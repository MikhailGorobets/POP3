#define _WIN32_WINNT 0x0A00  
#pragma warning(disable:4996)

#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

constexpr auto HOST = "pop.mail.ru";
constexpr auto PORT = "pop3s"; 




namespace POP3 {

	class Session;
	class Response;

	template<typename T> 
	struct ICommand { 
		auto Execute(Session& session) const -> Response;
	};

	struct CommandUser final: public ICommand<CommandUser> {
		CommandUser(std::string const& userName);
	    auto ExecuteImpl(Session& session) const -> Response;
	    std::string UserName;
	};

	struct CommandPass final: public ICommand<CommandPass> {
		CommandPass(std::string const& password);
		auto ExecuteImpl(Session& session) const -> Response;
		std::string Password;
	};

	struct CommandStat final: public ICommand<CommandStat> {
		auto ExecuteImpl(Session& session) const -> Response;
	};

	struct CommandList final: public ICommand<CommandList> {
		CommandList() = default;
		CommandList(uint64_t index);
		auto ExecuteImpl(Session& session) const -> Response;
		uint64_t Index = std::numeric_limits<uint64_t>::max();
	};

	struct CommandQuit final: public ICommand<CommandQuit> {
		auto ExecuteImpl(Session& session) const -> Response;
	};

	struct CommandDelete final: public ICommand<CommandDelete> {
		CommandDelete(uint64_t index);
		auto ExecuteImpl(Session& session) const -> Response;
		uint64_t Index;
	};

	struct CommandNoop final: public ICommand<CommandNoop> {
		auto ExecuteImpl(Session& session) const -> Response;
	};

	struct CommandRetr final: public ICommand<CommandRetr> {
		CommandRetr(uint64_t index);
		auto ExecuteImpl(Session& session) const -> Response;
		uint64_t Index;
	};

	struct CommandReset final: public ICommand<CommandReset> {
		auto ExecuteImpl(Session& session) const -> Response;
	};

	class Response {
	public:
		Response(std::string const& data);
		auto Status() const -> bool;
		auto Error()  const -> std::string;
	public:
		std::string Data;
	
	};

	class Session {		
	public:
		Session(std::string const& host, std::string const& port);
		template<typename T> auto ExecuteCommand(ICommand<T> const & cmd) -> Response;

	public:
		boost::asio::io_service                                IOService;
		boost::asio::ssl::context                              SSLContext;
		boost::asio::ip::tcp::resolver                         Resolver;
		boost::asio::ssl::stream<boost::asio::ip::tcp::socket> Socket;

	};

	std::ostream& operator<<(std::ostream& os, Response const& response);

}






int main(int argc, char* argv[]) {

	system("chcp 1251");
	
    auto constexpr user     = "misha.friman@mail.ru";
    auto constexpr password = "valovlox";

	try {

		POP3::Session session(HOST, PORT);
		std::cout << "Execute CommandUser: " << session.ExecuteCommand(POP3::CommandUser(user));
		std::cout << "Execute CommandPass: " << session.ExecuteCommand(POP3::CommandPass(password));
		std::cout << "Execute CommandStat: " << session.ExecuteCommand(POP3::CommandStat());
		std::cout << "Execute CommandList: " << session.ExecuteCommand(POP3::CommandList());
		std::cout << "Execute CommandRetr: " << session.ExecuteCommand(POP3::CommandRetr(10));
		std::cout << "Execute CommandQuit: " << session.ExecuteCommand(POP3::CommandQuit());

	} catch (std::exception const& e) {
		std::cerr << e.what();
	}

}






namespace POP3 {


	Response::Response(std::string const& data) : Data{ data } {}

	auto Response::Status() const -> bool {
		return true;
	}

	auto Response::Error() const -> std::string {
		return std::string();
	}


	std::ostream& operator<<(std::ostream& os, Response const& response) {
		os << response.Data;
		return os;
	}

	Session::Session(std::string const& host, std::string const& port)
		: IOService{}
		, SSLContext(boost::asio::ssl::context::tlsv12_client)
		, Resolver(IOService)
		, Socket(IOService, SSLContext)  {

		std::cout << "Run session -> Host: " + host + " Port: " + port << std::endl;

		boost::asio::connect(Socket.next_layer(), Resolver.resolve(host, port));
		Socket.handshake(boost::asio::ssl::stream_base::client);


		boost::asio::streambuf buffer;
		boost::asio::read(Socket, buffer, boost::asio::transfer_at_least(1));

		if (Response response{ std::string(std::istreambuf_iterator<char>(&buffer), std::istreambuf_iterator<char>()) }; !response.Status())
			throw std::runtime_error("Error: " + response.Error());


	}


	auto ExecuteCommand(Session& session, std::string const& data) -> Response {

		session.IOService.reset();
		boost::asio::streambuf buffer;
		boost::asio::steady_timer timer{ session.IOService, std::chrono::milliseconds(500) };
		boost::asio::write(session.Socket, boost::asio::buffer(data));


		auto waitTimeout = [](auto& session, auto& timer, auto& operationResult, auto& timerResult) -> bool {
			while (session.IOService.run_one()) {
				if (operationResult)
					timer.cancel();
				else if (timerResult)
					session.Socket.lowest_layer().cancel();
			}
			return (*operationResult) ? false : true;
		};

		while (true) {
			boost::system::error_code error;
			boost::optional<boost::system::error_code> sockResult;
			boost::optional<boost::system::error_code> timeResult;

			timer.async_wait([&](auto const& err) {
				timeResult.reset(err);
			});
			boost::asio::async_read(session.Socket, buffer, boost::asio::transfer_at_least(1), [&](auto const& err, auto size) {
				sockResult.reset(err);
			});
			session.IOService.reset();
			if (!waitTimeout(session, timer, sockResult, timeResult))
				break;
		}

		Response response{ std::string(std::istreambuf_iterator<char>(&buffer), std::istreambuf_iterator<char>()) };
		if (!response.Status())
			throw std::runtime_error("Error: " + response.Error());
		return response;
	}

	template<typename T>
	auto ICommand<T>::Execute(Session& session) const -> Response { return static_cast<T const*>(this)->ExecuteImpl(session); };

	CommandUser::CommandUser(std::string const& userName) : UserName{ userName } {}

	CommandPass::CommandPass(std::string const& password) : Password{ password } {}

	CommandList::CommandList(uint64_t index) : Index{ index } {}

	CommandDelete::CommandDelete(uint64_t index) : Index{ index } {}

	CommandRetr::CommandRetr(uint64_t index) : Index{ index } {}

	auto CommandDelete::ExecuteImpl(Session & session) const -> Response {
		return ExecuteCommand(session, std::string("DELE ") + std::to_string(Index) + std::string("\r\n"));
	}

	auto CommandUser::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, std::string("USER ") + UserName + std::string("\r\n"));
	}

	auto CommandPass::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, std::string("PASS ") + Password + std::string("\r\n"));
	}

	auto CommandStat::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "STAT\r\n");
	}

	auto CommandQuit::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "QUIT\r\n");
	}

	auto CommandList::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "LIST " + (Index == std::numeric_limits<uint64_t>::max() ? "" : std::to_string(Index)) + "\r\n");
	}

	auto CommandReset::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "RESET\r\n");
	}

	auto CommandNoop::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "NOOP\r\n");
	}

	auto CommandRetr::ExecuteImpl(Session& session) const -> Response {
		return ExecuteCommand(session, "RETR " + std::to_string(Index) + "\r\n");
	}

	template<typename T>
	auto Session::ExecuteCommand(ICommand<T> const& cmd) -> Response {
		return cmd.Execute(*this);

	}

}