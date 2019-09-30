#@ __global__
||

#@<OUT> result
NAME
      Result - Allows retrieving information about non query operations
               performed on the database.

DESCRIPTION
      An instance of this class will be returned on the CRUD operations that
      change the content of the database:

      - On Table: insert, update and delete
      - On Collection: add, modify and remove

      Other functions on the Session class also return an instance of this
      class:

      - Transaction handling functions
      - Transaction handling functions

PROPERTIES
      affected_item_count
            Same as get_affected_item_count

            ATTENTION: This property will be removed in a future release, use
                       the affected_items_count property instead.

      affected_items_count
            Same as get_affected_items_count

      auto_increment_value
            Same as get_auto_increment_value

      execution_time
            Same as get_execution_time

      generated_ids
            Same as get_generated_ids.

      warning_count
            Same as get_warning_count

            ATTENTION: This property will be removed in a future release, use
                       the warnings_count property instead.

      warnings
            Same as get_warnings

      warnings_count
            Same as get_warnings_count

FUNCTIONS
      get_affected_item_count()
            The the number of affected items for the last operation.

            ATTENTION: This function will be removed in a future release, use
                       the get_affected_items_count function instead.

      get_affected_items_count()
            The the number of affected items for the last operation.

      get_auto_increment_value()
            The last insert id auto generated (from an insert operation)

      get_execution_time()
            Retrieves a string value indicating the execution time of the
            executed operation.

      get_generated_ids()
            Returns the list of document ids generated on the server.

      get_warning_count()
            The number of warnings produced by the last statement execution.

            ATTENTION: This function will be removed in a future release, use
                       the get_warnings_count function instead.

      get_warnings()
            Retrieves the warnings generated by the executed operation.

      get_warnings_count()
            The number of warnings produced by the last statement execution.

      help([member])
            Provides help about this class and it's members

#@<OUT> result.affected_item_count
NAME
      affected_item_count - Same as get_affected_item_count

SYNTAX
      <Result>.affected_item_count

DESCRIPTION
      ATTENTION: This property will be removed in a future release, use the
                 affected_items_count property instead.

#@<OUT> result.affected_items_count
NAME
      affected_items_count - Same as get_affected_items_count

SYNTAX
      <Result>.affected_items_count

#@<OUT> result.auto_increment_value
NAME
      auto_increment_value - Same as get_auto_increment_value

SYNTAX
      <Result>.auto_increment_value

#@<OUT> result.execution_time
NAME
      execution_time - Same as get_execution_time

SYNTAX
      <Result>.execution_time

#@<OUT> result.generated_ids
NAME
      generated_ids - Same as get_generated_ids.

SYNTAX
      <Result>.generated_ids

#@<OUT> result.get_affected_item_count
NAME
      get_affected_item_count - The the number of affected items for the last
                                operation.

SYNTAX
      <Result>.get_affected_item_count()

RETURNS
      the number of affected items.

DESCRIPTION
      This is the value of the C API mysql_affected_rows(), see
      https://dev.mysql.com/doc/refman/en/mysql-affected-rows.html

      ATTENTION: This function will be removed in a future release, use the
                 get_affected_items_count function instead.

#@<OUT> result.get_affected_items_count
NAME
      get_affected_items_count - The the number of affected items for the last
                                 operation.

SYNTAX
      <Result>.get_affected_items_count()

RETURNS
      the number of affected items.

DESCRIPTION
      Returns the number of records affected by the executed operation

#@<OUT> result.get_auto_increment_value
NAME
      get_auto_increment_value - The last insert id auto generated (from an
                                 insert operation)

SYNTAX
      <Result>.get_auto_increment_value()

RETURNS
      the integer representing the last insert id

DESCRIPTION
      For more details, see
      https://dev.mysql.com/doc/refman/en/information-functions.html#function_last-insert-id

      Note that this value will be available only when the result is for a
      Table.insert operation.

#@<OUT> result.get_execution_time
NAME
      get_execution_time - Retrieves a string value indicating the execution
                           time of the executed operation.

SYNTAX
      <Result>.get_execution_time()

#@<OUT> result.get_generated_ids
NAME
      get_generated_ids - Returns the list of document ids generated on the
                          server.

SYNTAX
      <Result>.get_generated_ids()

RETURNS
      a list of strings containing the generated ids.

DESCRIPTION
      When adding documents into a collection, it is required that an ID is
      associated to the document, if a document is added without an '_id'
      field, an error will be generated.

      At MySQL 8.0.11 if the documents being added do not have an '_id' field,
      the server will automatically generate an ID and assign it to the
      document.

      This function returns a list of the IDs that were generated for the
      server to satisfy this requirement.

#@<OUT> result.get_warning_count
NAME
      get_warning_count - The number of warnings produced by the last statement
                          execution.

SYNTAX
      <Result>.get_warning_count()

RETURNS
      the number of warnings.

DESCRIPTION
      This is the same value than C API mysql_warning_count, see
      https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

      See get_warnings() for more details.

      ATTENTION: This function will be removed in a future release, use the
                 get_warnings_count function instead.

#@<OUT> result.get_warnings
NAME
      get_warnings - Retrieves the warnings generated by the executed
                     operation.

SYNTAX
      <Result>.get_warnings()

RETURNS
      A list containing a warning object for each generated warning.

DESCRIPTION
      This is the same value than C API mysql_warning_count, see
      https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

      Each warning object contains a key/value pair describing the information
      related to a specific warning.

      This information includes: Level, Code and Message.

#@<OUT> result.get_warnings_count
NAME
      get_warnings_count - The number of warnings produced by the last
                           statement execution.

SYNTAX
      <Result>.get_warnings_count()

RETURNS
      the number of warnings.

DESCRIPTION
      This is the same value than C API mysql_warning_count, see
      https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

      See get_warnings() for more details.

#@<OUT> result.help
NAME
      help - Provides help about this class and it's members

SYNTAX
      <Result>.help([member])

WHERE
      member: If specified, provides detailed information on the given member.

#@<OUT> result.warning_count
NAME
      warning_count - Same as get_warning_count

SYNTAX
      <Result>.warning_count

DESCRIPTION
      ATTENTION: This property will be removed in a future release, use the
                 warnings_count property instead.

#@<OUT> result.warnings
NAME
      warnings - Same as get_warnings

SYNTAX
      <Result>.warnings

#@<OUT> result.warnings_count
NAME
      warnings_count - Same as get_warnings_count

SYNTAX
      <Result>.warnings_count
