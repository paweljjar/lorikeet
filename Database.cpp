//
// Created by PAWEL on 15/10/2026.
//

#include "Database.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <sstream>
#include <vector>

namespace HTTPServer {
    Database::Database(std::string path, const bool createIfNotExists) : _path(std::move(path)), _createIfNotExists(createIfNotExists) { // Construct Database with the given file path and auto-creation flag, then load data
        _load(); // Load and parse database content from disk into memory
    }

    const std::string& Database::getPath() const { // Return the path of the database file
        return _path; // Return the internal filesystem path string
    }

    size_t Database::collectionCount() const { // Return the total count of top-level collections in the database
        return _data.size(); // Return the number of entries in the in-memory JSON object map
    }

    bool Database::hasTable(const std::string& tableName) const { // Check if a table exists in the database
        return _data.contains(tableName); // Return true if the table name exists in the internal data map
    }

    void Database::_validateScheme(const Json::Object& scheme) { // Validate that the provided table schema conforms to the required column structure
        if (scheme.empty()) { // Check if the schema object has no column definitions
            throw std::invalid_argument("Table scheme cannot be empty"); // Throw exception when no columns are specified
        }

        static const std::set<std::string> allowedTypes = { // Set of supported column data type names
            "integer", "string", "boolean", "float", "array", "object" // Allowed primitive and composite column types
        };
        static const std::set<std::string> booleanOptions = { // Set of known boolean constraint option keys
            "required", "primary_key", "auto_increment", "unique" // Optional boolean column modifiers
        };

        for (const auto& [columnName, columnDef] : scheme) { // Iterate over each column name and its definition in the schema
            if (columnName.empty()) { // Check if the column name key is an empty string
                throw std::invalid_argument("Column name cannot be empty"); // Throw exception if a column name is empty
            }

            if (!columnDef.isObject()) { // Ensure that the column definition is a Json::Object
                throw std::invalid_argument("Column '" + columnName + "' definition must be a JSON object"); // Throw exception if column value is not an object
            }

            const Json::Object& colObj = columnDef.asObject(); // Extract the underlying Json::Object map for the column definition
            auto typeIt = colObj.find("type"); // Look up the required "type" property in the column definition
            if (typeIt == colObj.end() || !typeIt->second.isString()) { // Verify that "type" exists and holds a string value
                throw std::invalid_argument("Column '" + columnName + "' must have a string 'type' property"); // Throw exception if "type" is missing or not a string
            }

            const std::string& typeName = typeIt->second.asString(); // Retrieve the string value of the "type" property
            if (!allowedTypes.contains(typeName)) { // Check if the specified type name is in the set of allowed types
                throw std::invalid_argument("Column '" + columnName + "' has unsupported type '" + typeName + "'"); // Throw exception for unsupported column type
            }

            for (const auto& [optKey, optVal] : colObj) { // Iterate over all properties in the column definition to validate options
                if (booleanOptions.contains(optKey) && !optVal.isBool()) { // Check if a boolean option key holds a non-boolean value
                    throw std::invalid_argument("Option '" + optKey + "' in column '" + columnName + "' must be a boolean"); // Throw exception if boolean constraint is not a bool
                }
            }

            if (colObj.contains("auto_increment") && colObj.at("auto_increment").asBool() && typeName != "integer") { // Ensure auto_increment is only enabled on integer columns
                throw std::invalid_argument("Column '" + columnName + "' with 'auto_increment' must have type 'integer'"); // Throw exception if non-integer column uses auto_increment
            }
        }
    }

    bool Database::_matchesType(const Json& value, const std::string& typeName) { // Check whether a Json value matches the expected schema type name
        if (typeName == "integer") { // Check if expected column type is an integer
            return value.isNumber() && std::isfinite(value.asNumber()) && std::floor(value.asNumber()) == value.asNumber(); // Verify value is a finite whole number
        }
        if (typeName == "float") { // Check if expected column type is a floating-point number
            return value.isNumber() && std::isfinite(value.asNumber()); // Verify value is a finite number
        }
        if (typeName == "string") { // Check if expected column type is a string
            return value.isString(); // Verify value holds a string
        }
        if (typeName == "boolean") { // Check if expected column type is a boolean
            return value.isBool(); // Verify value holds a boolean
        }
        if (typeName == "array") { // Check if expected column type is an array
            return value.isArray(); // Verify value holds a JSON array
        }
        if (typeName == "object") { // Check if expected column type is an object
            return value.isObject(); // Verify value holds a JSON object
        }
        return false; // Return false for any unrecognized type name
    }

    bool Database::createTable(const std::string& tableName, const Json::Object& scheme) { // Create a new table with the given name and schema in the database
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        _validateScheme(scheme); // Validate that the schema contains valid column objects with required "type" fields

        if (_data.contains(tableName)) { // Check if a table with the same name already exists in the database
            return false; // Return false to indicate table was not created because it already exists
        }

        Json::Object table; // Create an empty JSON object to represent the table structure
        table["scheme"] = scheme; // Assign the provided schema definition to the "scheme" property
        table["records"] = Json::Array{}; // Initialize an empty JSON array for the "records" property

        _data[tableName] = table; // Store the new table object in the database under the table name key
        _save(); // Persist the updated database state to disk

        return true; // Return true to indicate successful table creation
    }

    Json::Object Database::insert(const std::string& tableName, const Json::Object& record) { // Validate and insert a record into the specified table
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        Json::Object tableObj = _data.at(tableName).asObject(); // Retrieve a copy of the table JSON object
        const Json::Object& scheme = tableObj.at("scheme").asObject(); // Retrieve a const reference to the table schema definition
        Json::Array records = tableObj.at("records").asArray(); // Retrieve a copy of the existing records array

        for (const auto& key : record | std::views::keys) { // Iterate over all keys provided in the input record
            if (!scheme.contains(key)) { // Check if the key corresponds to a defined column in the table schema
                throw std::invalid_argument("Unknown column '" + key + "' in table '" + tableName + "'"); // Throw exception for unknown column names
            }
        }

        Json::Object newRecord; // Create a new JSON object to hold the validated and complete record

        for (const auto& [columnName, columnDef] : scheme) { // Iterate over each column definition in the table schema
            const Json::Object& colObj = columnDef.asObject(); // Extract the column schema object
            const std::string& typeName = colObj.at("type").asString(); // Retrieve the required type name for the column

            const bool isAutoIncrement = colObj.contains("auto_increment") && colObj.at("auto_increment").asBool(); // Check if column has auto_increment enabled
            const bool isPrimaryKey = colObj.contains("primary_key") && colObj.at("primary_key").asBool(); // Check if column is marked as a primary key
            const bool isRequired = (colObj.contains("required") && colObj.at("required").asBool()) || isPrimaryKey; // Check if column is required (explicitly or via primary_key)
            const bool isUnique = (colObj.contains("unique") && colObj.at("unique").asBool()) || isPrimaryKey; // Check if column values must be unique (explicitly or via primary_key)

            if (isAutoIncrement) { // Handle automatic value generation for auto_increment columns
                if (record.contains(columnName)) { // Check if caller attempted to manually provide an auto_increment column value
                    throw std::invalid_argument("Column '" + columnName + "' is auto_increment and cannot be manually specified"); // Forbid manual values for auto_increment fields
                }

                long long maxId = 0; // Track the highest existing integer value for this column across all records
                for (const auto& existingRow : records) { // Iterate through existing records in the table
                    const Json::Object& rowObj = existingRow.asObject(); // Extract the object map of the existing record
                    if (rowObj.contains(columnName) && rowObj.at(columnName).isNumber()) { // Check if the existing record has a numeric value for this column
                        const long long currentId = static_cast<long long>(rowObj.at(columnName).asNumber()); // Convert the existing numeric value to an integer
                        if (currentId > maxId) { // Compare current record's value against the running maximum
                            maxId = currentId; // Update running maximum when a larger value is found
                        }
                    }
                }

                newRecord[columnName] = maxId + 1; // Assign the next incremented integer value (starting at 1 when empty)
                continue; // Proceed to the next column in the schema
            }

            if (!record.contains(columnName) || record.at(columnName).isNull()) { // Check if the column is omitted or set to null in the input record
                if (isRequired) { // Check if the column is required by the schema
                    throw std::invalid_argument("Missing required column '" + columnName + "' in table '" + tableName + "'"); // Throw exception when a required column is missing or null
                }
                if (record.contains(columnName)) { // Check if the caller explicitly passed a null value for this optional column
                    newRecord[columnName] = nullptr; // Preserve the explicit null value in the stored record
                }
                continue; // Proceed to the next column in the schema
            }

            const Json& val = record.at(columnName); // Retrieve the provided value for the current column
            if (!_matchesType(val, typeName)) { // Validate that the provided value matches the column's declared type
                throw std::invalid_argument("Column '" + columnName + "' expects type '" + typeName + "'"); // Throw exception on type mismatch
            }

            if (isUnique) { // Check uniqueness constraint against all existing records in the table
                const std::string serializedVal = val.dump(); // Serialize the candidate value for exact comparison
                for (const auto& existingRow : records) { // Iterate through all existing records in the table
                    const Json::Object& rowObj = existingRow.asObject(); // Extract the object map of the existing record
                    if (rowObj.contains(columnName) && rowObj.at(columnName).dump() == serializedVal) { // Compare serialized values for equality
                        throw std::invalid_argument("Duplicate value for unique column '" + columnName + "' in table '" + tableName + "'"); // Throw exception if duplicate value is found
                    }
                }
            }

            newRecord[columnName] = val; // Store the validated column value into the new record object
        }

        records.push_back(newRecord); // Append the newly constructed record to the table's records array
        tableObj["records"] = std::move(records); // Update the "records" property in the table object
        _data[tableName] = std::move(tableObj); // Update the table entry in the in-memory database object
        _save(); // Persist the updated database state to disk

        return newRecord; // Return the inserted record including generated auto_increment fields
    }

    Json::Array Database::select(const std::string& tableName, const Json::Object& where) const { // Query and return records from the specified table matching the given filter criteria
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        const Json::Object& tableObj = _data.at(tableName).asObject(); // Retrieve a const reference to the table JSON object
        const Json::Object& scheme = tableObj.at("scheme").asObject(); // Retrieve a const reference to the table schema definition
        const Json::Array& records = tableObj.at("records").asArray(); // Retrieve a const reference to the table's records array

        if (where.empty()) { // Check if no filter criteria were specified
            return records; // Return all records in the table when the filter is empty
        }

        for (const auto& [key, val] : where) { // Validate each filter key and value against the table schema
            if (!scheme.contains(key)) { // Check if the filter column exists in the table schema
                throw std::invalid_argument("Unknown column '" + key + "' in table '" + tableName + "'"); // Throw exception for unknown filter column
            }

            const std::string& typeName = scheme.at(key).asObject().at("type").asString(); // Retrieve the expected type name for the filter column
            if (!val.isNull() && !_matchesType(val, typeName)) { // Ensure non-null filter values match the column's declared type
                throw std::invalid_argument("Filter value for column '" + key + "' expects type '" + typeName + "'"); // Throw exception if filter value has wrong type
            }
        }

        Json::Array matched; // Array to accumulate records that satisfy all filter criteria

        for (const auto& row : records) { // Iterate through each record in the table
            const Json::Object& rowObj = row.asObject(); // Extract the object map of the current record
            bool isMatch = true; // Flag indicating whether the current record matches all conditions in "where"

            for (const auto& [key, expectedVal] : where) { // Check each key-value condition in the filter object
                if (expectedVal.isNull()) { // Check if the filter condition is matching null / omitted values
                    if (rowObj.contains(key) && !rowObj.at(key).isNull()) { // If the record contains a non-null value for this column
                        isMatch = false; // Mark record as non-matching
                        break; // Stop checking remaining filter conditions for this record
                    }
                } else { // Filter condition expects a specific non-null value
                    if (!rowObj.contains(key) || rowObj.at(key).dump() != expectedVal.dump()) { // Compare serialized record value with expected value
                        isMatch = false; // Mark record as non-matching if missing or different
                        break; // Stop checking remaining filter conditions for this record
                    }
                }
            }

            if (isMatch) { // Check if all filter conditions were satisfied
                matched.push_back(row); // Add the matching record to the result array
            }
        }

        return matched; // Return the array of matching records
    }

    Json::Array Database::select(const std::string& tableName, const std::function<bool(const Json::Object&)>& where) const { // Query and return records from the specified table matching the given predicate callback
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        if (!where) { // Check if the provided predicate callback is empty/null
            throw std::invalid_argument("Filter callback cannot be null"); // Throw exception if callback is not callable
        }

        const Json::Object& tableObj = _data.at(tableName).asObject(); // Retrieve a const reference to the table JSON object
        const Json::Array& records = tableObj.at("records").asArray(); // Retrieve a const reference to the table's records array

        Json::Array matched; // Array to accumulate records for which the predicate returns true

        for (const auto& row : records) { // Iterate through each record in the table
            if (where(row.asObject())) { // Invoke the predicate callback with the current record's Json::Object
                matched.push_back(row); // Append the record to the result array if the predicate returned true
            }
        }

        return matched; // Return the array of matching records
    }

    Json::Array Database::update(const std::string& tableName, const Json::Object& updates, const Json::Object& where) { // Update records matching the key-value filter criteria and return the updated records
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        const Json::Object& scheme = _data.at(tableName).asObject().at("scheme").asObject(); // Retrieve a const reference to the table schema definition

        for (const auto& [key, val] : where) { // Validate each filter key and value against the table schema
            if (!scheme.contains(key)) { // Check if the filter column exists in the table schema
                throw std::invalid_argument("Unknown column '" + key + "' in table '" + tableName + "'"); // Throw exception for unknown filter column
            }

            const std::string& typeName = scheme.at(key).asObject().at("type").asString(); // Retrieve the expected type name for the filter column
            if (!val.isNull() && !_matchesType(val, typeName)) { // Ensure non-null filter values match the column's declared type
                throw std::invalid_argument("Filter value for column '" + key + "' expects type '" + typeName + "'"); // Throw exception if filter value has wrong type
            }
        }

        return update(tableName, updates, [&where](const Json::Object& rowObj) { // Delegate to the predicate callback overload using the validated key-value filter
            for (const auto& [key, expectedVal] : where) { // Check each key-value condition in the filter object
                if (expectedVal.isNull()) { // Check if the filter condition is matching null / omitted values
                    if (rowObj.contains(key) && !rowObj.at(key).isNull()) { // If the record contains a non-null value for this column
                        return false; // Return false because the record does not match the null condition
                    }
                } else { // Filter condition expects a specific non-null value
                    if (!rowObj.contains(key) || rowObj.at(key).dump() != expectedVal.dump()) { // Compare serialized record value with expected value
                        return false; // Return false if the column is missing or its value differs
                    }
                }
            }
            return true; // Return true when all conditions in "where" are satisfied
        });
    }

    Json::Array Database::update(const std::string& tableName, const Json::Object& updates, const std::function<bool(const Json::Object&)>& where) { // Update records matching the predicate callback and return the updated records
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        if (updates.empty()) { // Check if the updates object has no column changes specified
            throw std::invalid_argument("Update data cannot be empty"); // Throw exception when updates map is empty
        }

        if (!where) { // Check if the provided predicate callback is empty/null
            throw std::invalid_argument("Filter callback cannot be null"); // Throw exception if callback is not callable
        }

        Json::Object tableObj = _data.at(tableName).asObject(); // Retrieve a copy of the table JSON object
        const Json::Object& scheme = tableObj.at("scheme").asObject(); // Retrieve a const reference to the table schema definition
        Json::Array records = tableObj.at("records").asArray(); // Retrieve a working copy of the existing records array

        for (const auto& [colName, newVal] : updates) { // Validate each column and value in the updates object against the schema
            if (!scheme.contains(colName)) { // Check if the column exists in the table schema
                throw std::invalid_argument("Unknown column '" + colName + "' in table '" + tableName + "'"); // Throw exception for unknown column name
            }

            const Json::Object& colObj = scheme.at(colName).asObject(); // Extract the column schema definition
            const bool isPrimaryKey = colObj.contains("primary_key") && colObj.at("primary_key").asBool(); // Check if column is marked as a primary key
            const bool isAutoIncrement = colObj.contains("auto_increment") && colObj.at("auto_increment").asBool(); // Check if column has auto_increment enabled
            const bool isRequired = colObj.contains("required") && colObj.at("required").asBool(); // Check if column is marked as required

            if (isPrimaryKey) { // Forbid modifying primary key columns
                throw std::invalid_argument("Column '" + colName + "' is a primary_key and cannot be updated"); // Throw exception when attempting to update a primary_key
            }

            if (isAutoIncrement) { // Forbid modifying auto_increment columns
                throw std::invalid_argument("Column '" + colName + "' is auto_increment and cannot be updated"); // Throw exception when attempting to update an auto_increment column
            }

            if (newVal.isNull()) { // Check if the update value is null
                if (isRequired) { // Forbid setting a required column to null
                    throw std::invalid_argument("Required column '" + colName + "' in table '" + tableName + "' cannot be set to null"); // Throw exception when setting required column to null
                }
            } else { // Update value is non-null, so validate its type
                const std::string& typeName = colObj.at("type").asString(); // Retrieve the expected type name for the column
                if (!_matchesType(newVal, typeName)) { // Check if the new value matches the declared column type
                    throw std::invalid_argument("Column '" + colName + "' expects type '" + typeName + "'"); // Throw exception on type mismatch
                }
            }
        }

        std::vector<size_t> updatedIndices; // Track indices of records modified during this update operation
        Json::Array updatedRecords; // Array to collect the updated records to return to the caller

        for (size_t i = 0; i < records.size(); ++i) { // Iterate through all existing records by index
            Json::Object rowObj = records[i].asObject(); // Retrieve a mutable copy of the current record object
            if (where(rowObj)) { // Check if the current record satisfies the filter predicate
                for (const auto& [colName, newVal] : updates) { // Apply each column update to the matched record
                    rowObj[colName] = newVal; // Assign the new column value in the record
                }
                records[i] = rowObj; // Store the modified record back into the working records array
                updatedIndices.push_back(i); // Record the index of this modified row for uniqueness verification
                updatedRecords.push_back(std::move(rowObj)); // Append the updated record to the result array
            }
        }

        if (updatedRecords.empty()) { // Check if no records matched the filter criteria
            return updatedRecords; // Return empty array without writing to disk
        }

        for (const auto& [colName, newVal] : updates) { // Verify uniqueness constraints for any updated unique columns
            if (newVal.isNull()) continue; // Null values do not violate uniqueness constraints
            const Json::Object& colObj = scheme.at(colName).asObject(); // Extract the column schema definition
            const bool isUnique = colObj.contains("unique") && colObj.at("unique").asBool(); // Check if column has unique constraint enabled
            if (!isUnique) continue; // Skip columns that do not require unique values

            const std::string serializedVal = newVal.dump(); // Serialize the new value for exact equality comparison
            for (const size_t idx : updatedIndices) { // Check each modified record against all other records in the table
                for (size_t j = 0; j < records.size(); ++j) { // Iterate over all records in the working array
                    if (idx == j) continue; // Do not compare a record against itself
                    const Json::Object& otherRow = records[j].asObject(); // Extract the other record's object map
                    if (otherRow.contains(colName) && otherRow.at(colName).dump() == serializedVal) { // Check if another record holds the same value
                        throw std::invalid_argument("Duplicate value for unique column '" + colName + "' in table '" + tableName + "'"); // Throw exception on unique constraint violation
                    }
                }
            }
        }

        tableObj["records"] = std::move(records); // Update the "records" property in the table object
        _data[tableName] = std::move(tableObj); // Update the table entry in the in-memory database object
        _save(); // Persist the updated database state to disk

        return updatedRecords; // Return the array of updated records
    }

    Json::Array Database::remove(const std::string& tableName, const Json::Object& where) { // Delete records matching the key-value filter criteria and return the removed records
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        const Json::Object& scheme = _data.at(tableName).asObject().at("scheme").asObject(); // Retrieve a const reference to the table schema definition

        for (const auto& [key, val] : where) { // Validate each filter key and value against the table schema
            if (!scheme.contains(key)) { // Check if the filter column exists in the table schema
                throw std::invalid_argument("Unknown column '" + key + "' in table '" + tableName + "'"); // Throw exception for unknown filter column
            }

            const std::string& typeName = scheme.at(key).asObject().at("type").asString(); // Retrieve the expected type name for the filter column
            if (!val.isNull() && !_matchesType(val, typeName)) { // Ensure non-null filter values match the column's declared type
                throw std::invalid_argument("Filter value for column '" + key + "' expects type '" + typeName + "'"); // Throw exception if filter value has wrong type
            }
        }

        return remove(tableName, [&where](const Json::Object& rowObj) { // Delegate to the predicate callback overload using the validated key-value filter
            for (const auto& [key, expectedVal] : where) { // Check each key-value condition in the filter object
                if (expectedVal.isNull()) { // Check if the filter condition is matching null / omitted values
                    if (rowObj.contains(key) && !rowObj.at(key).isNull()) { // If the record contains a non-null value for this column
                        return false; // Return false because the record does not match the null condition
                    }
                } else { // Filter condition expects a specific non-null value
                    if (!rowObj.contains(key) || rowObj.at(key).dump() != expectedVal.dump()) { // Compare serialized record value with expected value
                        return false; // Return false if the column is missing or its value differs
                    }
                }
            }
            return true; // Return true when all conditions in "where" are satisfied
        });
    }

    Json::Array Database::remove(const std::string& tableName, const std::function<bool(const Json::Object&)>& where) { // Delete records matching the predicate callback and return the removed records
        if (tableName.empty()) { // Check if the provided table name is empty
            throw std::invalid_argument("Table name cannot be empty"); // Throw exception if table name is empty
        }

        if (!_data.contains(tableName)) { // Check if the target table exists in the database
            throw std::invalid_argument("Table '" + tableName + "' does not exist"); // Throw exception if table does not exist
        }

        if (!where) { // Check if the provided predicate callback is empty/null
            throw std::invalid_argument("Filter callback cannot be null"); // Throw exception if callback is not callable
        }

        Json::Object tableObj = _data.at(tableName).asObject(); // Retrieve a copy of the table JSON object
        const Json::Array& records = tableObj.at("records").asArray(); // Retrieve a const reference to the existing records array

        Json::Array remainingRecords; // Array to hold records that do not match the deletion filter
        Json::Array deletedRecords; // Array to collect the removed records to return to the caller

        for (const auto& row : records) { // Iterate through each existing record in the table
            if (where(row.asObject())) { // Check if the current record satisfies the deletion predicate
                deletedRecords.push_back(row); // Add the matching record to the deleted records array
            } else { // Record does not match the deletion filter
                remainingRecords.push_back(row); // Keep the record in the remaining records array
            }
        }

        if (!deletedRecords.empty()) { // Check if at least one record was removed
            tableObj["records"] = std::move(remainingRecords); // Update the "records" property with the remaining records
            _data[tableName] = std::move(tableObj); // Update the table entry in the in-memory database object
            _save(); // Persist the updated database state to disk
        }

        return deletedRecords; // Return the array of removed records
    }

    void Database::save() const { // Persist in-memory database content to disk
        _save(); // Serialize and write database data to the storage file
    }

    void Database::_load() { // Load and parse JSON content from the database file on disk, creating a new file if configured
        if (!std::filesystem::exists(_path)) { // Check if the database file does not exist on disk
            if (_createIfNotExists) { // Check if auto-creation of a missing database file is enabled
                std::cout << "No database file at " << _path << ", creating new one" << std::endl; // Log an informational message indicating that a new database file is being created

                std::ofstream outfile(_path); // Open an output file stream to create the new database file

                if (!outfile) { // Verify if the file stream was opened successfully
                    throw std::runtime_error("Failed to create database file at " + _path); // Throw an exception if file creation fails
                }

                outfile.write("{}", 2); // Write an empty JSON object literal ("{}") as initial database content
                outfile.close(); // Close the output file stream after writing

                return; // Return early since the new empty database is now initialized
            }
            throw std::runtime_error("Database file not found: " + _path); // Throw an exception if the file does not exist and creation is not allowed
        }

        std::ifstream file(_path, std::ios::binary); // Open an input file stream in binary mode to read the database file
        if (!file) { // Verify that the database file was opened successfully
            throw std::runtime_error("Cannot open database file: " + _path); // Throw an exception if the database file cannot be opened
        }

        std::stringstream buffer; // String stream buffer to read entire file content
        buffer << file.rdbuf(); // Read all bytes from the file stream buffer into the string stream
        const std::string content = buffer.str(); // Extract the accumulated content as a standard string

        if (content.empty()) return; // If file content is empty, return early without parsing

        Json parsed = Json::parse(content); // Parse the file content string using the JSON parser
        if (!parsed.isObject()) { // Validate that the parsed JSON root structure is a JSON object
            throw std::runtime_error("Database file is corrupted: expected a JSON object at top level"); // Throw an exception if root JSON value is not an object
        }

        _data = parsed.asObject(); // Store the parsed JSON object map in the internal database data container
        std::cout << "Loaded database from " << _path // Log successful database loading with path
                  << " (" << _data.size() << " collections)" << std::endl; // Output the total count of collections loaded from the database
    }

    void Database::_save() const { // Serialize and persist in-memory database data to storage file on disk
        std::ofstream file(_path, std::ios::trunc); // Open database file stream in truncation mode to overwrite content
        if (!file) { // Verify that the database file was opened successfully for writing
            throw std::runtime_error("Cannot open database file for writing: " + _path); // Throw an exception if writing fails
        }

        file << Json(_data).dump(4); // Write formatted JSON string with 4-space indentation to file
        file.close(); // Close the output file stream after writing
    }
} // HTTPServer
