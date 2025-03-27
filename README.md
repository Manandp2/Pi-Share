# Simple C Network Project

This project demonstrates a simple network connection between two devices using C. It consists of a client and a server that communicate over a TCP socket.

## Project Structure

```
simple-c-network
├── src
│   ├── client.c
│   ├── server.c
│   └── common.h
├── Makefile
└── README.md
```

## Prerequisites

- A C compiler (e.g., gcc)
- Basic knowledge of C programming
- Access to a terminal or command prompt

## Compilation

To compile the client and server, navigate to the project directory and run the following command:

```
make
```

This will generate two executables: `client` and `server`.

## Running the Server

Before running the client, you need to start the server. In the terminal, execute:

```
./server
```

The server will start and listen for incoming connections on the specified port.

## Running the Client

Once the server is running, open another terminal window and execute:

```
./client <server_ip> <port>
```

Replace `<server_ip>` with the IP address of the machine running the server and `<port>` with the port number specified in the code.

## Example

1. Start the server on one device.
2. Run the client on another device, providing the server's IP address and port.
3. The client will send a message to the server, and the server will respond.

## Notes

- Ensure that the firewall settings allow traffic on the specified port.
- This example uses TCP sockets for communication.