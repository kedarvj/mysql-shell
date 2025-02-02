/*
 * Copyright (c) 2022, 2023, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "modules/adminapi/cluster/add_replica_instance.h"

#include <cassert>
#include <exception>
#include <memory>

#include "modules/adminapi/cluster/cluster_impl.h"
#include "modules/adminapi/common/async_topology.h"
#include "modules/adminapi/common/common.h"
#include "modules/adminapi/common/dba_errors.h"
#include "modules/adminapi/common/instance_validations.h"
#include "modules/adminapi/common/metadata_storage.h"
#include "modules/adminapi/common/undo.h"
#include "mysqlshdk/include/scripting/types.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/libs/utils/logger.h"

namespace mysqlsh::dba::cluster {

std::shared_ptr<Instance> Add_replica_instance::get_source_instance(
    const std::string &source) {
  std::shared_ptr<Instance> source_instance;

  try {
    source_instance = m_cluster_impl->get_session_to_cluster_instance(source);
  } catch (const shcore::Exception &e) {
    mysqlsh::current_console()->print_error(
        "Unable to use '" + source +
        "' as a source, instance is unreachable: " + e.format());

    throw shcore::Exception(
        "Source is not reachable",
        SHERR_DBA_READ_REPLICA_INVALID_SOURCE_LIST_UNREACHABLE);
  }

  return source_instance;
}

void Add_replica_instance::validate_instance_is_standalone() {
  TargetType::Type instance_type;
  // Check if the instance's metadata is empty, i.e. the instance is standalone
  if (validate_instance_standalone(*m_target_instance, &instance_type)) return;

  if (instance_type == TargetType::InnoDBCluster) {
    // Check if the instance is part of this Cluster first. If not, check if
    // the Cluster is part of a ClusterSet to check if the instance is part of
    // any Cluster of it
    if (m_cluster_impl->is_instance_cluster_member(*m_target_instance)) {
      mysqlsh::current_console()->print_error(
          "The instance '" + m_target_instance->descr() +
          "' is already part of this Cluster. A new "
          "Read-Replica must be created on a standalone instance.");

      throw shcore::Exception(
          "Target instance already part of this InnoDB Cluster",
          SHERR_DBA_BADARG_INSTANCE_MANAGED_IN_CLUSTER);
    } else if (m_cluster_impl->is_cluster_set_member() &&
               m_cluster_impl->is_instance_cluster_set_member(
                   *m_target_instance)) {
      mysqlsh::current_console()->print_error(
          "The instance '" + m_target_instance->descr() +
          "' is already part of a Cluster of the ClusterSet. A new "
          "Read-Replica must be created on a standalone instance.");

      throw shcore::Exception(
          "Target instance already part of an InnoDB Cluster",
          SHERR_DBA_BADARG_INSTANCE_MANAGED_IN_CLUSTER);
    }

    // Check if the instance's Metadata includes itself as part of this Cluster
    auto i_metadata = std::make_shared<MetadataStorage>(m_target_instance);

    Cluster_metadata instance_cluster_md;
    if (i_metadata->get_cluster_for_server_uuid(m_target_instance->get_uuid(),
                                                &instance_cluster_md)) {
      // The instance's metadata indicates it belongs to this Cluster, so it
      // must have been forcefully removed and should be possible to add it
      // back
      if (instance_cluster_md.cluster_id == m_cluster_impl->get_id()) return;

      if (std::string cs_id;
          m_cluster_impl->get_metadata_storage()->check_cluster_set(
              nullptr, nullptr, nullptr, &cs_id)) {
        std::vector<Cluster_set_member_metadata> cs_members;

        if (m_cluster_impl->get_metadata_storage()->get_cluster_set(
                cs_id, false, nullptr, &cs_members)) {
          auto it = std::find_if(
              cs_members.begin(), cs_members.end(),
              [&cs_id = instance_cluster_md.cluster_id](const auto &member) {
                return (cs_id == member.cluster.cluster_id);
              });
          // The instance's metadata includes this instance as part of a
          // ClusterSet member so it must have been forcefully removed from it
          // and should be possible to add it back to the same or another
          // Cluster of the ClusterSet
          if (it != cs_members.end()) return;
        }
      }
    }
  }

  throw shcore::Exception(
      "Target instance already part of an " + to_string(instance_type),
      SHERR_DBA_BADARG_INSTANCE_MANAGED_IN_CLUSTER);
}

Member_recovery_method Add_replica_instance::validate_instance_recovery() {
  auto check_recoverable =
      [=](const mysqlshdk::mysql::IInstance &tgt_instance) {
        // Get the gtid state in regards to the donor
        mysqlshdk::mysql::Replica_gtid_state state =
            check_replica_group_gtid_state(*m_donor_instance, tgt_instance,
                                           nullptr, nullptr);

        return (state != mysqlshdk::mysql::Replica_gtid_state::IRRECOVERABLE);
      };

  return mysqlsh::dba::validate_instance_recovery(
      Cluster_type::GROUP_REPLICATION, Member_op_action::ADD_INSTANCE,
      *m_donor_instance, *m_target_instance, check_recoverable,
      m_options.clone_options.recovery_method.get_safe(),
      m_cluster_impl->get_gtid_set_is_complete(),
      current_shell_options()->get().wizards);
}

void Add_replica_instance::validate_source_list() {
  if (!m_options.replication_sources_option.source_type.has_value()) {
    // If the 'replicationSources' is not used, the default value is "primary"
    m_options.replication_sources_option.source_type = Source_type::PRIMARY;
  }

  // If the replicationSources is set to "secondary", verify whether the
  // Cluster has at least 1 SECONDARY member
  if (m_options.replication_sources_option.source_type ==
      Source_type::SECONDARY) {
    auto online_instances = m_cluster_impl->get_active_instances(true);
    if (online_instances.size() == 1) {
      mysqlsh::current_console()->print_error(
          "Unable to set the 'replicationSources' to 'secondary': the Cluster "
          "does not have any ONLINE SECONDARY member.");
      throw shcore::Exception(
          "No ONLINE SECONDARY members available",
          SHERR_DBA_READ_REPLICA_INVALID_SOURCE_LIST_NOT_ONLINE);
    }
  }
}

std::shared_ptr<Instance> Add_replica_instance::get_default_source_instance() {
  // When replicationSources is set to 'primary' or 'secondary' the main source
  // is the primary member of the cluster
  if (m_options.replication_sources_option.source_type ==
          Source_type::PRIMARY ||
      m_options.replication_sources_option.source_type ==
          Source_type::SECONDARY) {
    return m_cluster_impl->get_cluster_server();
  }

  // When replicationSources is set to a list of instances the main source must
  // be the first member of the list (the one with highest weight)
  auto first_element =
      m_options.replication_sources_option.replication_sources.begin();

  std::string source_str = first_element->to_string();

  // Validate all sources are reachable, belong to the Cluster, and are
  // ONLINE
  std::shared_ptr<Instance> main_source;

  for (const auto &source :
       m_options.replication_sources_option.replication_sources) {
    std::string source_string = source.to_string();

    auto source_instance = get_source_instance(source.to_string());
    Instance_metadata instance_md;

    // If we can connect to the instance, validate if:
    //   a) The instance belongs to the MD
    //   b) The instance is not a Read-Replica
    //   c) The instance is ONLINE

    try {
      // Check if the address is in the Metadata

      // Use the canonical address, since that's the one stored in the MD
      auto source_canononical_address =
          source_instance->get_canonical_address();

      instance_md =
          m_cluster_impl->get_metadata_storage()->get_instance_by_address(
              source_canononical_address);
    } catch (const shcore::Exception &e) {
      mysqlsh::current_console()->print_error(
          "Unable to use '" + source_string +
          "' as a source, instance does not belong to the Cluster: " +
          e.format());

      throw shcore::Exception(
          "Source does not belong to the Cluster",
          SHERR_DBA_READ_REPLICA_INVALID_SOURCE_LIST_NOT_IN_MD);
    }

    // Check if the instance is a Read-Replica
    if (instance_md.instance_type == Instance_type::READ_REPLICA) {
      mysqlsh::current_console()->print_error(
          "Unable to use '" + source_string +
          "' as a source: instance is a Read-Replica of the Cluster");

      throw shcore::Exception(
          "Invalid source type: Read-Replica",
          SHERR_DBA_READ_REPLICA_INVALID_SOURCE_LIST_NOT_CLUSTER_MEMBER);
    }

    // Check if the instance is ONLINE
    auto state = mysqlshdk::gr::get_member_state(*source_instance);

    if (state != mysqlshdk::gr::Member_state::ONLINE) {
      mysqlsh::current_console()->print_error(
          "Unable to use '" + source_str +
          "' as a source: instance's state is '" + to_string(state) + "'");
      throw shcore::Exception(
          "Source is not ONLINE",
          SHERR_DBA_READ_REPLICA_INVALID_SOURCE_LIST_NOT_ONLINE);
    }

    // The main source must be the first member of the 'replicationSources'
    if (!main_source) {
      main_source = std::move(source_instance);
    }

    // Keep checking the list
    continue;
  }

  return main_source;
}

void Add_replica_instance::check_ssl_mode() {
  Cluster_ssl_mode ssl_mode = to_cluster_ssl_mode(
      m_cluster_impl->get_cluster_server()->get_sysvar_string(
          "group_replication_ssl_mode", ""));

  switch (ssl_mode) {
    case Cluster_ssl_mode::AUTO:
      // Not possible
      assert(0);
      break;

    case Cluster_ssl_mode::VERIFY_CA:
      mysqlsh::current_console()->print_error(
          "The Cluster has TLS (encryption) enabled with SSL certificate "
          "issuer verification (VERIFY_CA) which is currently not supported in "
          "Read-Replicas.");
      throw shcore::Exception::runtime_error(
          "Unsupported Cluster's sslMode: VERIFY_CA");

    case Cluster_ssl_mode::VERIFY_IDENTITY:
      mysqlsh::current_console()->print_error(
          "The Cluster has TLS (encryption) enabled with SSL certificate "
          "subject verification (VERIFY_IDENTITY) which is not supported in "
          "Read-Replicas.");
      throw shcore::Exception::runtime_error(
          "Unsupported Cluster's sslMode: VERIFY_IDENTITY");

    case Cluster_ssl_mode::NONE:
    case Cluster_ssl_mode::DISABLED:
    case Cluster_ssl_mode::REQUIRED:
      log_info("SSL mode used to configure the Read-Replica: '%s'",
               to_string(ssl_mode).c_str());
  }
}

void Add_replica_instance::do_run() {
  auto console = mysqlsh::current_console();

  console->print_info("Setting up '" + m_target_instance->descr() +
                      "' as a Read Replica of Cluster '" +
                      m_cluster_impl->get_name() + "'.");
  console->print_info();

  // Validations and variables initialization
  {
    // The target instance must comply with the following requirements to
    // become a Read Replica:
    //
    //  - Instance and Cluster must be running MySQL >= 8.0.23
    //  - Must be a standalone instance
    //  - Must not have any pre-existing replication channels configured
    //  - Its `server_id` and `server_uuid` must be unique in the topology
    //  - Must have been pre-configured with the clusterAdmin account of the
    //    Cluster
    //  - The Cluster's sslMode must not be VERIFY_CA of VERIFY_IDENTIFY
    //  - The Cluster's memberAuthTypo must be PASSWORD

    // Check if the Cluster and instance are running MySQL >= 8.0.23
    cluster_topology_executor_ops::validate_read_replica_version(
        m_target_instance->get_version(),
        m_cluster_impl->get_lowest_instance_version());

    // Check if the instance already belongs to any topology
    validate_instance_is_standalone();

    bool using_clone_recovery = m_options.clone_options.recovery_method ==
                                Member_recovery_method::CLONE;

    // Check instance configuration:
    //   - Compliance with InnoDB Cluster settings
    //     (checkInstanceConfiguration())
    //   - Check if there are illegal async replication channels
    //   - Check instance server UUID and ID (must be unique among the cluster
    //     members)
    m_cluster_impl->check_instance_configuration(
        m_target_instance, using_clone_recovery,
        checks::Check_type::READ_REPLICA, false);

    // Validate the recovery options and set the recovery method
    m_options.clone_options.check_option_values(
        m_target_instance->get_version());

    // Validate and resolve the SSL mode
    check_ssl_mode();

    // Check if the Cluster's memberAuthType is different than password
    if (auto auth_type = m_cluster_impl->query_cluster_auth_type();
        auth_type != Replication_auth_type::PASSWORD) {
      mysqlsh::current_console()->print_error(
          "The Cluster is configured to use an authentication type different "
          "than '" +
          to_string(Replication_auth_type::PASSWORD) +
          "' which is not supported in Read-Replicas.");
      throw shcore::Exception::runtime_error(
          "Unsupported Cluster's memberAuthType: '" + to_string(auth_type) +
          "'");
    }

    // Check the replicationSources option
    validate_source_list();

    // Get the main donor and source instance
    // By default, the instance is:
    //
    //   - The primary member when 'replicationSources' is set to "primary" or
    //   "secondary"
    //   - The first member of the 'replicationSources' list
    m_donor_instance = get_default_source_instance();

    // If the cloneDonor option us used, validate if the selected donor is
    // valid and set it as the donor instance
    if (*m_options.clone_options.recovery_method ==
            Member_recovery_method::CLONE &&
        m_options.clone_options.clone_donor.has_value()) {
      std::string donor = *m_options.clone_options.clone_donor;

      m_donor_instance =
          Scoped_instance(m_cluster_impl->connect_target_instance(donor));
    }

    // Check if the donor is valid
    m_cluster_impl->ensure_compatible_clone_donor(*m_donor_instance,
                                                  *m_target_instance);

    console->print_info("* Checking transaction state of the instance...");
    m_options.clone_options.recovery_method = validate_instance_recovery();
  }

  // Create the Read-Replica:
  //
  //  - Create the replication account
  //  - Handle clone provisioning
  //  - Update metadata
  //  - Setup the managed async replication channel
  {
    // Create the replication user
    Async_replication_options ar_options;
    std::string repl_account_host;

    std::tie(ar_options, repl_account_host) =
        m_cluster_impl->create_read_replica_replication_user(
            m_target_instance.get(), "", m_options.timeout, m_options.dry_run);

    m_undo_tracker.add("Dropping replication account", [=]() {
      log_info("Dropping replication account '%s'",
               ar_options.repl_credentials->user.c_str());
      m_cluster_impl->drop_read_replica_replication_user(
          m_target_instance.get(), m_options.dry_run);
    });

    // Handle clone provisioning
    if (*m_options.clone_options.recovery_method ==
        Member_recovery_method::CLONE) {
      // If clone monitoring is aborted with ^C exactly during the phase on
      // which clone is dropping data, there's a chance that the Metadata
      // is only partially deleted. If the operation is retried, that
      // broken metadata will cause unexpected SQL errors while reading from it.
      // To avoid that, we drop the Metadata schema from the target instance
      // when clone is aborted.
      auto &clone_cleanup = m_undo_tracker.add_back(
          "Dropping Metadata schema from Read-Replica", [=]() {
            try {
              bool restore_super_read_only = false;
              if (m_target_instance->get_sysvar_bool("super_read_only",
                                                     false)) {
                m_target_instance->set_sysvar(
                    "super_read_only", false,
                    mysqlshdk::mysql::Var_qualifier::GLOBAL);
                restore_super_read_only = true;
              }

              metadata::uninstall(m_target_instance);

              if (restore_super_read_only) {
                m_target_instance->set_sysvar(
                    "super_read_only", true,
                    mysqlshdk::mysql::Var_qualifier::GLOBAL);
              }
            } catch (std::exception &e) {
              log_info("Failed to drop Metadata schema from Read-Replica: %s",
                       e.what());
            }
          });

      m_cluster_impl->handle_clone_provisioning(
          m_target_instance, m_donor_instance, ar_options, repl_account_host,
          "", "", m_options.get_recovery_progress(), m_options.timeout,
          m_options.dry_run);

      // Cancel this step from the undo_list since it's only necessary if clone
      // is cancelled/fails
      clone_cleanup.cancel();

      // Clone will copy all tables, including the replication settings stored
      // in mysql.slave_master_info. MySQL will start replication by default
      // if the replication setting are not empty, so in a fast system or if
      // --skip-slave-start is not enabled replication will start and the
      // slave threads will be up-and-running before we issue the new CHANGE
      // MASTER. This will result in an error: MySQL Error 3081 (HY000): (...)
      // This operation cannot be performed with running replication threads;
      // run STOP SLAVE FOR CHANNEL '' first
      //
      // To avoid this situation, we must stop the slave and reset the
      // replication channels being the only possibility the ClusterSet
      // channel if the donor belongs to a Replica Cluster
      remove_channel(*m_target_instance, k_clusterset_async_channel_name,
                     m_options.dry_run);
    }

    // we need a point in time as close as possible, but still earlier than
    // when recovery starts to monitor the recovery phase. The timestamp
    // resolution is timestamp(3) irrespective of platform
    std::string join_begin_time =
        m_target_instance->queryf_one_string(0, "", "SELECT NOW(3)");

    // Update Metadata
    if (!m_options.dry_run) {
      auto add_read_replica_trx_undo = Transaction_undo::create();

      MetadataStorage::Transaction trx(m_cluster_impl->get_metadata_storage());

      m_cluster_impl->add_metadata_for_instance(
          *m_target_instance, Instance_type::READ_REPLICA,
          m_options.label.value_or(""), add_read_replica_trx_undo.get(),
          m_options.dry_run);

      m_cluster_impl->get_metadata_storage()->update_instance_attribute(
          m_target_instance->get_uuid(), k_instance_attribute_join_time,
          shcore::Value(join_begin_time), false,
          add_read_replica_trx_undo.get());

      // Store the replication account
      m_cluster_impl->get_metadata_storage()->update_read_replica_repl_account(
          m_target_instance->get_uuid(), ar_options.repl_credentials->user,
          repl_account_host, add_read_replica_trx_undo.get());

      shcore::Value source_list_md;

      if (m_options.replication_sources_option.source_type ==
          Source_type::PRIMARY) {
        source_list_md = shcore::Value("PRIMARY");
      } else if (m_options.replication_sources_option.source_type ==
                 Source_type::SECONDARY) {
        source_list_md = shcore::Value("SECONDARY");
      } else if (m_options.replication_sources_option.source_type ==
                 Source_type::CUSTOM) {
        shcore::Array_t source_list_array = shcore::make_array();

        // Copy of the replicationSources list to store the list with the
        // canonical addresses
        auto replication_sources_updated =
            m_options.replication_sources_option.replication_sources;

        for (const auto &source :
             m_options.replication_sources_option.replication_sources) {
          auto source_instance = get_source_instance(source.to_string());

          std::string source_canononical_address =
              source_instance->get_canonical_address();

          if (source_canononical_address != source.to_string()) {
            Managed_async_channel_source src(source_canononical_address,
                                             source.weight);

            replication_sources_updated.erase(source);

            replication_sources_updated.insert(std::move(src));
          }

          source_list_array->push_back(
              shcore::Value(source_canononical_address));
        }

        m_options.replication_sources_option.replication_sources =
            replication_sources_updated;

        source_list_md = shcore::Value(std::move(source_list_array));
      }

      // Store the replicationSources
      m_cluster_impl->get_metadata_storage()->update_instance_attribute(
          m_target_instance->get_uuid(),
          k_instance_attribute_read_replica_replication_sources,
          shcore::Value(source_list_md), false,
          add_read_replica_trx_undo.get());

      trx.commit();
      log_debug("addReplicaInstance() metadata updates done");

      m_undo_tracker.add(
          "Removing Read-Replica's metadata",
          Sql_undo_list(std::move(add_read_replica_trx_undo)), [this]() {
            return m_cluster_impl->get_metadata_storage()->get_md_server();
          });
    }

    log_debug("Read-Replica add finished");

    // Setup the target instance as a read-replica of the cluster's primary
    console->print_info(
        "* Configuring Read-Replica managed replication channel...");

    m_cluster_impl->setup_read_replica(m_target_instance.get(), ar_options,
                                       m_options.replication_sources_option,
                                       false, m_options.dry_run);

    // Remove the channel last, to ensure the revert updates are
    // propagated
    m_undo_tracker.add_back("Removing Read-Replica replication channel", [=]() {
      remove_channel(*m_target_instance, k_read_replica_async_channel_name,
                     m_options.dry_run);
      reset_managed_connection_failover(*m_target_instance, m_options.dry_run);
    });

    // Synchronize with source
    try {
      console->print_info();
      console->print_info("* Waiting for Read-Replica '" +
                          m_target_instance->descr() +
                          "' to synchronize with Cluster...");

      if (!m_options.dry_run) {
        m_cluster_impl->sync_transactions(*m_target_instance.get(),
                                          Instance_type::READ_REPLICA,
                                          m_options.timeout);
      }
    } catch (const shcore::Exception &e) {
      if (e.code() == SHERR_DBA_GTID_SYNC_TIMEOUT) {
        console->print_warning(
            "The Read-Replica failed to synchronize its transaction set "
            "with the Cluster. You may increase or disable the "
            "transaction sync timeout with the option 'timeout'");
      }
      throw;
    } catch (const cancel_sync &) {
      // Throw it up
      throw;
    }
  }

  console->print_info();
  console->print_info("'" + m_target_instance->descr() +
                      "' successfully added as a Read-Replica of Cluster '" +
                      m_cluster_impl->get_name() + "'.");
  console->print_info();

  if (m_options.dry_run) {
    console->print_info("dryRun finished.");
    console->print_info();
  }
}

}  // namespace mysqlsh::dba::cluster
