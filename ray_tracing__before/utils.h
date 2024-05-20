#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <json.hpp>
#include <string>

// Función para eliminar comentarios de una cadena JSON
std::string remove_comments(const std::string& input);
nlohmann::json getJSONPartitionFromFile(std::string fileName, std::string partitionName);

#endif // UTILS_H