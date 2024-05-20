#include "utils.h"
#include <regex>
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

std::string remove_comments(const std::string& input) {
	// Elimina comentarios de una cadena JSON utilizando una expresión regular
	std::regex regex_comments(R"((\/\/[^\n]*)|(/\*[\s\S]*?\*/))");
	return std::regex_replace(input, regex_comments, "");
}

//Funcion que retorna un JSON con todo el contenido de una seccion a partir de "/" (root)
nlohmann::json getJSONPartitionFromFile(std::string fileName, std::string partitionName)
{
	nlohmann::json jsonPartition;

	std::ifstream file(fileName);
	if (!file.is_open())
	{
		std::cout << "Error al abrir el archivo de configuración" << std::endl;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string jsonContent = buffer.str();

	std::string jsonWhithoutComments = remove_comments(jsonContent);
	try {
		nlohmann::json jsonConfig = nlohmann::json::parse(jsonWhithoutComments);
		file.close();

		jsonPartition = jsonConfig[partitionName];
	}
	catch (const std::exception& e) {
		std::cout << "Error: " << e.what() << std::endl;
	}
	return jsonPartition;
}