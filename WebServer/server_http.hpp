#pragma once
#ifdef  WIN32	
#define _WIN32_WINNT 0x0501
#endif


#include <boost/asio.hpp>
#include <boost/algorithm/algorithm.hpp>
#include <unordered_map>
#include <boost/regex.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <thread>



template<typename socket_type>
class server_base
{
protected:
	boost::asio::io_service io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;
	//unsigned short port_;
	long timeout_request_;
	long timeout_content_;
	std::vector<std::thread> threads_;
public:
	server_base(unsigned short port,size_t num_threads,long timeout_request,long timeout_send_or_receive)
		:acceptor_(io_service_)
		,config_(port,num_threads)
		,timeout_request_(timeout_request)
		,timeout_content_(timeout_send_or_receive)
	{
	}
	~server_base()
	{
	}

	class Response : public std::ostream
	{
		friend class server_base<socket_type>;

	public :
		boost::asio::streambuf streambuf_;

		std::shared_ptr<socket_type> socket_;
	public:
		Response(std::shared_ptr<socket_type> socket)
			:std::ostream(&streambuf_) //清空数据
			, socket_(socket)
		{
		
		}
	public:
		size_t size()
		{
			return streambuf_.size();
		}
	};

	virtual void accept() = 0;

	void start()
	{
		opt_resource.clear();
		for (auto& res : resource_)
		{
			for (auto& res_method : res.second)
			{
				auto it = opt_resource.end();
				for (auto opt_it = opt_resource.begin(); opt_it != opt_resource.end(); opt_it++)
				{
				}

				if (it == opt_resource.end())
				{
					opt_resource.emplace_back();

					it = opt_resource.begin() + (opt_resource.size() - 1);

					it->first = res_method.first;
				}
				it->second.emplace_back(boost::regex(res.first), res_method.second);
			}
			
		}




	  if (io_service_.stopped())
	  {
		  io_service_.reset();
	  }

	  boost::asio::ip::tcp::endpoint endpoint;
	 endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), config_.port_);

	  acceptor_.open(endpoint.protocol());
	  acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
	  acceptor_.bind(endpoint);
	  acceptor_.listen();

	  accept();


	  threads_.clear();
	  for (size_t c = 1;c < config_.num_threads_;c++)
	  {
		  threads_.emplace_back(
			  [this]()
			  {
				  io_service_.run();
			  }
		  );
	  }

	  io_service_.run();

	  for (auto& t : threads_)
	  {
		  t.join();
	  }
	}
	
	void stop()
	{
		acceptor_.close();
		io_service_.stop();
	}

	void send(std::shared_ptr<Response>response, const std::function<void(const boost::system::error_code&)>& callback = nullptr )const
	{
		boost::asio::async_write(*response->socket_, response->streambuf_,
			[this, callback](const boost::system::error_code& error,size_t bytes_transferred)
				{
					if (callback)
					{
						callback(error);
					}
				}
			);
	}

	class Content :public std::istream
	{
		friend class server_base<socket_type>;
	private:
	    boost::asio::streambuf& streambuf_;
	public:
		Content(boost::asio::streambuf& streambuf)
				:std::istream(&streambuf)
				, streambuf_(streambuf)
				{}
	public:
		size_t size()
		{
			return streambuf_.size();
		}

		std::string string()
		{
			std::stringstream ss;
			ss << rdbuf();
			return ss.str();
		}
	};

	class Request
	{
		friend class server_base<socket_type>;
	private:
		boost::asio::streambuf streambuf_;
		
	
	public:
		Content content_;
		Request()
			:content_(streambuf_)
		{}

		class iequal_to
		{
		public:
			bool operator()(const std::string& key1, const std::string& key2)const
			{
				return boost::algorithm::iequals(key1, key2);
			}
		};

		class ihash
		{
		public:
			size_t operator()(const std::string& key) const
			{
				std::size_t hashvalue = 0;
				for (auto c : key)
				{
					boost::hash_combine(hashvalue, std::tolower(c));
				}
				return hashvalue;
			}
		};
	public:
		boost::smatch path_match_;
		std::string remote_endpoint_address;
		unsigned short remote_endpoint_port;
		std::string method_, path_, http_version_;
		std::unordered_multimap<std::string, std::string, ihash, iequal_to> header_;
	};

	

	class Config
	{
		friend class server_base<socket_type>;
	private:
		bool reuse_address_;
		size_t num_threads_;
		std::string address_;
		unsigned short port_;
	public:
		Config(unsigned short port, size_t num_threads)
			:num_threads_(num_threads)
			, port_(port)
			,reuse_address_(true)
		{
		}
    };

	Config config_;

	std::function<void(const std::exception&)> exception_handler;

	std::unordered_map<std::string, std::unordered_map<std::string, std::function<void(std::shared_ptr< typename server_base<socket_type>::Response> response, std::shared_ptr< typename server_base<socket_type>::Request> request)> > > resource_;

	std::unordered_map<std::string, std::function<void(std::shared_ptr< typename server_base<socket_type>::Response> response, std::shared_ptr< typename server_base<socket_type>::Request> request)> > default_resource_;

	private:
		std::vector< std::pair < std::string, std::vector < std::pair < boost::regex,std::function<void(std::shared_ptr<typename server_base<socket_type>::Response>response, std::shared_ptr<typename server_base<socket_type>::Request> request) > > > > > opt_resource;
   public:
	void read_request_and_contect(std::shared_ptr<socket_type> socket)
	{

		std::shared_ptr<Request>request(new Request);
		try
		{

			request->remote_endpoint_address = socket->lowest_layer().remote_endpoint().address().to_string();
			request->remote_endpoint_port = socket->lowest_layer().remote_endpoint().port();
			std::cout << "客户端IP：" << request->remote_endpoint_address<<std::endl << "端口为:" << request->remote_endpoint_port<<std::endl;
		}
		catch (const std::exception& e)
		{
			if (exception_handler)
			{
				exception_handler(e);
			}
		}

		std::shared_ptr<boost::asio::deadline_timer> timer;

		if (timeout_request_ > 0)
		{
			/*std::cout << "定时器被安装\n";*/
			timer = set_timeout_on_socket(socket, timeout_request_);
		}
		 
		boost::asio::async_read_until(*socket, request->streambuf_, "\r\n\r\n",
			[this,socket,request,timer](const boost::system::error_code& error,size_t bytes_transferred)
			{
		
					if (timeout_request_ > 0)
					{
						/*std::cout << "定时器被取消\n";*/
						timer->cancel();
					}
					if (!error)
					{
						size_t num_additional_bytes = request->streambuf_.size() - bytes_transferred;
						//std::cout << "附加数据大小为：" << num_additional_bytes << std::endl;

						if (!parse_request(request, request->content_))
							return;
					       
						auto it = request->header_.find("Content-Length");
						if (it != request->header_.end())
						{
								std::shared_ptr<boost::asio::deadline_timer> timer;
								if (timeout_content_ > 0)
								{
									timer = set_timeout_on_socket(socket, timeout_content_);
								}
								unsigned long long content_length;

								try
								{
									content_length = stoull(it->second);
								}
								catch (const std::exception& e)
								{
									if (exception_handler)
									{
										exception_handler(e);
									}

									return;
								}

								if (content_length > num_additional_bytes)
									{
										boost::asio::async_read(*socket, request->streambuf_,
											boost::asio::transfer_exactly(content_length - num_additional_bytes),
											[this, socket, request, timer](const boost::system::error_code& error, size_t bytes_transferred)
										{
											if (timeout_content_ > 0)
											{
												timer->cancel();
											}
											if (!error)
											{
												find_resource(socket, request);
											}
										});
									}
								else
									{
										find_resource(socket, request);
									}
						}
						else
						{
							find_resource(socket, request);
						}
					}
			});
	}

	void find_resource(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request)
	{
		 for (auto& res : opt_resource)
		 {
			 if (request->method_ == res.first)
			 {
				 for (auto& res_path : res.second)
				 {
					 boost::smatch sm_res;
					 if (boost::regex_match(request->path_,sm_res, res_path.first))
					 {
						 request->path_match_ = std::move(sm_res);
						 write_response(socket,request,res_path.second);
						 return;
					 }
				 }
			 }
		 }
		 auto it_method = default_resource_.find(request->method_);
		 if (it_method != default_resource_.end())
		 {
			 write_response(socket, request, it_method->second);
		 }
	}

	void write_response(std::shared_ptr<socket_type> socket,
		std::shared_ptr<Request> request,
		std::function<void(std::shared_ptr< typename server_base<socket_type>::Response> response, std::shared_ptr< typename server_base<socket_type>::Request> request)>& resource_function)
	{
		
		std::shared_ptr<boost::asio::deadline_timer> timer;
		if (timeout_content_ > 0)
			timer = set_timeout_on_socket(socket, timeout_content_);

		auto response = std::shared_ptr<Response>(new Response(socket), [this, request, timer](Response* response_ptr)
		{
			auto response = std::shared_ptr<Response>(response_ptr);
			send(response, [this, response, request, timer](const boost::system::error_code& ec)
			{
				if (!ec)
				{
					if (timeout_content_ > 0)
						timer->cancel();
					float http_version;
					try
					{
						http_version = stof(request->http_version_);
					}
					catch (const std::exception& e)
					{
						if (exception_handler)
							exception_handler(e);
						return;

					}

					auto range = request->header_.equal_range("Connection");
					for (auto it = range.first; it != range.second; it++)
					{
						if (boost::iequals(it->second, "close"))
							return;
					}
					if (http_version > 1.05)
						read_request_and_contect(response->socket_);
				}
			});
		});
		try
		{
			resource_function(response, request);
		}
		catch (const std::exception& e)
		{
			if (exception_handler)
				exception_handler(e);
			return;

		}
	}

	bool parse_request(std::shared_ptr<Request> request, std::istream& stream)const
	{
		std::string line;
		getline(stream,line);
		size_t method_end;

		
		if ((method_end = line.find(' ')) != std::string::npos)
		{
	
			size_t path_end;
			if ((path_end = line.find(' ', method_end + 1)) != std::string::npos)
			{
				request->method_ = line.substr(0, method_end);
				request->path_ = line.substr(method_end + 1, path_end - method_end - 1);

				size_t protocol_end;
				if ((protocol_end = line.find('/', path_end + 1)) != std::string::npos)
				{
					if (line.substr(path_end + 1, protocol_end - path_end - 1) != "HTTP")
					{
						return false;
					}
					request->http_version_ = line.substr(protocol_end + 1, line.size() - protocol_end - 2);
				}

				std::cout << "方法:" << request->method_
					<< "\n路径:" << request->path_
					<< "\n版本号:" << request->http_version_
					<< std::endl;

				getline(stream, line);
				size_t parm_end;
				while ((parm_end = line.find(':')) != std::string::npos)
				{
					size_t value_start = parm_end + 1;
					if (value_start < line.size())
					{
						if (line[value_start] == ' ')
						{
							value_start++;
						}
						if (value_start < line.size())
						{
							std::cout << "--------------\n";
							printf("%s\n", line.substr(0, parm_end).c_str());
							printf("%s\n", line.substr(value_start, line.size() - value_start - 1).c_str());
							request->header_.insert(
								std::make_pair(line.substr(0, parm_end).c_str(), line.substr(value_start, line.size() - value_start - 1))
							);
						}

						getline(stream, line);
					}
				}

			}
			else
				return false;
		}
		else false;

		return true;
	}



	

	std::shared_ptr<boost::asio::deadline_timer> set_timeout_on_socket(std::shared_ptr<socket_type> socket, long seconds)
	{
		std::shared_ptr<boost::asio::deadline_timer> timer(std::make_shared<boost::asio::deadline_timer>(io_service_));
		timer->expires_from_now(boost::posix_time::seconds(seconds));
		timer->async_wait(
			[socket](const boost::system::error_code& error) 
		{
		if (!error)
			{
				boost::system::error_code ec;
				socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
				socket->lowest_layer().close();
			}
		});
		return timer;
	}
};

template<typename socket_type>
class server :public server_base<socket_type>
{
public:
protected:
private:
};


typedef boost::asio::ip::tcp::socket HTTP;
template<>
class server<HTTP> : public server_base<HTTP>
{
public:
	server(unsigned short port,size_t num_threads = 1,long timeout_request = 5,long timeout_content = 300)
		:server_base<HTTP>::server_base(port,num_threads, timeout_request, timeout_content)
	{
	
	}

	virtual void accept()
	{
		std::shared_ptr<HTTP> socket(std::make_shared<HTTP>(io_service_));

		acceptor_.async_accept(*socket,
			[this,socket](const boost::system::error_code& error) 
				{
					accept();
					if (!error)
					{
						boost::asio::ip::tcp::no_delay option(true);
						socket->set_option(option);
						read_request_and_contect(socket);
					}	
				}
            );

	}
};