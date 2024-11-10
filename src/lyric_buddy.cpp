#include <iostream>
#include <curl/curl.h>
#include <json/json.h>
#include <sstream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <limits.h>

std::unordered_map<std::string, std::string> parseEnvFile(const std::string &envFilePath)
{
	std::unordered_map<std::string, std::string> envMap;

	std::ifstream envFile(envFilePath);

	if (!envFile.is_open())
	{
		throw std::runtime_error("Could not open .env file: " + envFilePath);
	}

	std::string line;

	while (std::getline(envFile, line))
	{
		if (line.empty() || line[0] == '#')
		{
			continue;
		}

		std::istringstream keyValuePair(line);
		std::string key, value;

		if (std::getline(keyValuePair, key, '=') && std::getline(keyValuePair, value))
		{
			envMap[key] = value;
		}
	}

	envFile.close();

	return envMap;
}

std::string getAPIKeyFromSOPS(const std::string &filename)
{
	std::cout << "Checkpoint: Starting SOPS decryption for file: " << filename << std::endl;

	std::string command = "sops -d " + filename;
	std::cout << "Executing command: " << command << std::endl;

	FILE *pipe = popen(command.c_str(), "r");

	if (!pipe)
	{
		throw std::runtime_error("Failed to run SOPS decryption command.");
	}

	char buffer[128];

	std::string decryptedContent;

	std::cout << "Checkpoint: Reading decrypted content..." << std::endl;

	while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
	{
		decryptedContent += buffer;
	}

	int returnCode = pclose(pipe);

	if (returnCode != 0)
	{
		throw std::runtime_error("SOPS decryption command failed with return code: " + std::to_string(returnCode));
	}

	if (decryptedContent.empty())
	{
		std::cerr << "Error: Decrypted content is empty. Ensure the file is valid and SOPS is configured properly." << std::endl;

		throw std::runtime_error("Failed to decrypt secrets file. Ensure SOPS is configured properly.");
	}

	std::cout << "Checkpoint: Successfully decrypted content: Length: " << decryptedContent.length() << " characters." << std::endl;

	Json::CharReaderBuilder builder;
	Json::Value config;

	std::string errs;
	std::istringstream stream(decryptedContent);

	std::cout << "Checkpoint: Parsing JSON..." << std::endl;

	if (!Json::parseFromStream(builder, stream, &config, &errs))
	{
		throw std::runtime_error("Error parsing decrypted JSON: " + errs);
	}

	return config["api_key"].asString();
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output)
{
	size_t totalSize = size * nmemb;

	output->append((char *)contents, totalSize);

	return totalSize;
}

std::string callOpenAI(const std::string &prompt, const std::string &apiKey, int maxTokens, const std::string &model, double temperature)
{
	std::cout << "Sending prompt to OpenAI: " << prompt << std::endl;

	CURL *curl = curl_easy_init();
	if (!curl)
	{
		throw std::runtime_error("Failed to initialize cURL.");
	}

	std::string apiUrl = "https://api.openai.com/v1/chat/completions";
	std::string readBuffer;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
	headers = curl_slist_append(headers, "Content-Type: application/json");

	Json::Value payload;

	payload["model"] = model;
	payload["messages"] = Json::arrayValue;

	Json::Value systemMessage;
	systemMessage["role"] = "system";
	systemMessage["content"] = "You're my songwriting partner! I'm going to send you ideas, lyrics, or song concepts. Please help me turn these scraps into lyric ideas. Thanks!";

	Json::Value userMessage;
	userMessage["role"] = "user";
	userMessage["content"] = prompt;

	payload["messages"].append(systemMessage);
	payload["messages"].append(userMessage);

	payload["max_tokens"] = maxTokens;
	payload["temperature"] = temperature;

	Json::StreamWriterBuilder writer;
	std::string requestBody = Json::writeString(writer, payload);

	curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK)
	{
		throw std::runtime_error("cURL request failed: " + std::string(curl_easy_strerror(res)));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	std::cout << "Received response: " << readBuffer << std::endl;

	return readBuffer;
}

int main()
{
	try
	{
		char cwd[PATH_MAX];

		if (getcwd(cwd, sizeof(cwd)) == nullptr)
		{
			throw std::runtime_error("Failed to get current working directory.");
		}

		std::string envFilePath = std::string(cwd) + "/config/.env";
		std::unordered_map<std::string, std::string> envVars = parseEnvFile(envFilePath);

		std::string secretsFilePath = std::string(cwd) + "/" + envVars["SECRETS_FILE"];
		std::cout << "Resolved secrets file path: " << secretsFilePath << std::endl;

		std::string apiKey = getAPIKeyFromSOPS(secretsFilePath);

		int maxTokens = std::stoi(envVars["MAX_TOKENS"]);
		double temperature = std::stod(envVars["TEMPERATURE"]);
		std::string model = envVars["MODEL"];

		bool continuePrompting = true;

		while (continuePrompting)
		{
			std::string mainPrompt, songDetails;

			std::cout << "Enter a lyric idea prompt: ";
			std::getline(std::cin, mainPrompt);

			std::cout << "Describe the song's style, tempo, mood, etc.: ";
			std::getline(std::cin, songDetails);

			std::string fullPrompt = "Main idea: " + mainPrompt + "\nStyle details: " + songDetails;

			std::string response = callOpenAI(fullPrompt, apiKey, maxTokens, model, temperature);

			Json::CharReaderBuilder builder;
			Json::Value jsonResponse;

			std::string errs;
			std::istringstream stream(response);

			if (Json::parseFromStream(builder, stream, &jsonResponse, &errs))
			{
				std::cout << "AI Response: " << jsonResponse["choices"][0]["message"]["content"].asString() << std::endl;
			}
			else
			{
				throw std::runtime_error("Failed to parse AI response JSON: " + errs);
			}

			while (true)
			{
				std::string userChoice;
				std::cout << "Would you like to enter another prompt? (yes/y or no/n): ";
				std::getline(std::cin, userChoice);

				for (auto &c : userChoice)
				{
					c = std::tolower(c);
				}

				if (userChoice == "no" || userChoice == "n")
				{
					continuePrompting = false;
					std::cout << "Goodbye!" << std::endl;

					break;
				}
				else if (userChoice == "yes" || userChoice == "y")
				{
					continuePrompting = true;

					break;
				}
				else
				{
					std::cout << "Invalid response. Please enter 'yes', 'y', 'no', or 'n'." << std::endl;
				}
			}
		}
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Error: " << ex.what() << std::endl;

		return 1;
	}

	return 0;
}
