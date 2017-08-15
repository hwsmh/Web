#include "server_http.hpp"
#include <iostream>
#include <thread>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>

using namespace std;
using namespace boost::property_tree;

typedef server<HTTP> HttpServer;


void default_resource_send(const HttpServer &server, shared_ptr<HttpServer::Response> response,
	shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer);

int main(int argc, char** argv)
{

	HttpServer server(8181, 1);


	server.resource_["^/string$"]["POST"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	
		auto content = request->content_.string();


		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
	};

	server.resource_["^/json$"]["POST"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
		try {
			ptree pt;
			read_json(request->content_, pt);

			string name = pt.get<string>("firstName") + " " + pt.get<string>("lastName");

			*response << "HTTP/1.1 200 OK\r\n"
				<< "Content-Type: application/json\r\n"
				<< "Content-Length: " << name.length() << "\r\n\r\n"
				<< name;
		}
		catch (exception& e) {
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
		}
	};



	server.resource_["^/info$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		stringstream content_stream;
		content_stream << "<h1>Request from " << request->remote_endpoint_address << " (" << request->remote_endpoint_port << ")</h1>";
		content_stream << request->method_ << " " << request->path_ << " HTTP/" << request->http_version_ << "<br>";
		for (auto& header : request->header_) {
			content_stream << header.first << ": " << header.second << "<br>";
		}


		content_stream.seekp(0, ios::end);

		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
	};


	server.resource_["^/match/([0-9]+)$"]["GET"] = [&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		string number = request->path_match_[1];
		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << number.length() << "\r\n\r\n" << number;
	};


	server.resource_["^/work$"]["GET"] = [&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> /*request*/) {
		thread work_thread([response] {
			this_thread::sleep_for(chrono::seconds(5));
			string message = "Work done";
			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
		});
		work_thread.detach();
	};


	server.resource_["/reg$"]["POST"] = [&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		stringstream content_stream;
		content_stream << "<h1>Request from " << request->remote_endpoint_address << " (" << request->remote_endpoint_port << ")</h1>";
		content_stream << request->method_ << " " << request->path_ << " HTTP/" << request->http_version_ << "<br>";
		for (auto& header : request->header_) {
			content_stream << header.first << ": " << header.second << "<br>";
		}

		content_stream << "<br>";

		content_stream << request->content_.string();

		content_stream.seekp(0, ios::end);

		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
	};

	server.default_resource_["GET"] = [&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		try {
			auto web_root_path = boost::filesystem::canonical("web");
			auto path = boost::filesystem::canonical(web_root_path / request->path_);
	
			if (distance(web_root_path.begin(), web_root_path.end()) > distance(path.begin(), path.end()) ||
				!equal(web_root_path.begin(), web_root_path.end(), path.begin()))
				throw invalid_argument("path must be within root path");
			if (boost::filesystem::is_directory(path))
				path /= "index.html";
			if (!(boost::filesystem::exists(path) && boost::filesystem::is_regular_file(path)))
				throw invalid_argument("file does not exist");

			auto ifs = make_shared<ifstream>();
			ifs->open(path.string(), ifstream::in | ios::binary);

			if (*ifs) {
		
				streamsize buffer_size = 131072;
				auto buffer = make_shared<vector<char> >(buffer_size);

				ifs->seekg(0, ios::end);
				auto length = ifs->tellg();

				ifs->seekg(0, ios::beg);

				*response << "HTTP/1.1 200 OK\r\nContent-Length: " << length << "\r\n\r\n";
				default_resource_send(server, response, ifs, buffer);
			}
			else
				throw invalid_argument("could not read file");
		}
		catch (const exception &e) {
			string content = "Could not open path " + request->path_ + ": " + e.what();
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
		}
	};

	thread server_thread([&server]() {

		server.start();
	});


	this_thread::sleep_for(chrono::seconds(1));

	server_thread.join();

	return 0;
}


void default_resource_send(const HttpServer &server, shared_ptr<HttpServer::Response> response,
	shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer)
{
	streamsize read_length;
	if ((read_length = ifs->read(&(*buffer)[0], buffer->size()).gcount()) > 0) {
		response->write(&(*buffer)[0], read_length);
		if (read_length == static_cast<streamsize>(buffer->size())) {
			server.send(response, [&server, response, ifs, buffer](const boost::system::error_code &ec) {
				if (!ec)
					default_resource_send(server, response, ifs, buffer);
				else
					cerr << "Connection interrupted" << endl;
			});
		}
	}
}
