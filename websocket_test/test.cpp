#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// Generates a random JSON message
std::string generateRandomJSONMessage() {
  // Example JSON format: {"data": "random_data"}
  std::srand(std::time(nullptr));
  std::string randomData = "random_data_" + std::to_string(std::rand());
  return "{\"data\": \"" + randomData + "\"}";
}

int main() {
  try {
    // Normal boost::asio setup
    net::io_context io_context;

    // These objects perform our I/O
    tcp::resolver resolver(io_context);
    websocket::stream<tcp::socket> ws(io_context);

    // Look up the domain name
    auto const results = resolver.resolve("10.35.5.199", "3465");

    // Make the connection on the IP address we get from a lookup
    net::connect(ws.next_layer(), results.begin(), results.end());

    // Perform the websocket handshake
    ws.handshake("10.35.5.199", "/testing/you/");

    // Generate a random JSON message
    std::string message = generateRandomJSONMessage();

    // Send the message
    ws.write(net::buffer(message));

    // This buffer will hold the incoming message
    beast::flat_buffer buffer;

    // Read a message into our buffer
    ws.read(buffer);

    // Close the WebSocket connection
    ws.close(websocket::close_code::normal);

    // If we get here, the connection is closed gracefully
    std::cout << beast::make_printable(buffer.data()) << std::endl;
  } catch (std::exception const &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
