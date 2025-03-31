# Pi-Share 341 Project

## Compilation

To compile the client and server, navigate to the project directory and run the following command:

```
make
```

This will generate two executables: `client` and `server`.

## Running the Server

Before running the client, you need to start the server. In the terminal, execute:

```bash
./server
```

The server will start and listen for incoming connections on the specified port.

## Running the Client

Once the server is running, open another terminal window and execute:

```bash
./client <server_ip> <filename(optional)>
```

Replace `<server_ip>` with the IP address of the machine running the server and `<filename(optional)>` with the filename you want to request. Otherwise, you will be prompted for the filename when the program runs.

## Example

1. Start the server on one device.
2. Run the client on another device, providing the server's IP address and port.
3. The client will ask the server for a file, and the server will send it back.
4. Currently, the client is set up to only accept one file and then close.

## Details
All files are downloaded to/shared from the `pi-sharing` directory.