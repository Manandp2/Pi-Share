# Pi-Share 341 Project

## Overview

Pi-Share is a distributed file sharing system developed for CS 341. It allows clients to upload, download, list, and delete files from a central server, with support for sub-servers to distribute load and storage. The system uses TCP sockets and supports redirection for scalability.

## Compilation

To compile the client and server, navigate to the project directory and run the following command:

```bash
  make
```

This will generate two executables: `client` and `server`.

## Running the Main Server

Before running the client, you need to start the server. In the terminal, execute:

```bash
  ./server <port>
```

The server will start and listen for incoming connections on the specified port.

## Running a Sub-Sever

```bash
  ./client <main_server_ip>:<main_server_port> ADD_SERVER
```

This will send information about your sub-server to the main server and start the sub-server on your computer.

## Running the Client

Once the server is running, open another terminal window and execute:

```bash
  ./client <server_ip>:<server_port> <method> <remote_file> <local_file>
```
If more information is required, run the client without any arguments for usage help.

Replace server_ip and server_port with the main server's information.

## Example

### 1. Start the Main Server

```bash
./server 9000
```

This will create a `Pi-Share` directory (if it doesn't exist) and listen for connections on port 9000.

---

### 2. Upload a File to the Server (PUT)

Suppose you have a file called `hello.txt` in your current directory.

```bash
./client 127.0.0.1:9000 PUT hello.txt hello.txt
```

- This uploads `hello.txt` from your local machine to the server, storing it as `hello.txt` on the server.

---

### 3. List Files on the Server (LIST)

```bash
./client 127.0.0.1:9000 LIST
```

- This will print the list of files currently available on the server.

---

### 4. Download a File from the Server (GET)

```bash
./client 127.0.0.1:9000 GET hello.txt downloaded.txt
```

- This downloads `hello.txt` from the server and saves it as `downloaded.txt` on your local machine.

---

### 5. Delete a File from the Server (DELETE)

```bash
./client 127.0.0.1:9000 DELETE hello.txt
```

- This deletes `hello.txt` from the server.

---

### 6. Register a Sub-Server

Suppose you want to add a sub-server running on the same machine (using port 8080):

```bash
./client 127.0.0.1:9000 ADD_SERVER
```

- This will register your machine as a sub-server with the main server and start a new server instance on port 8080.

Alternatively, if you want to start a sub-server on another computer,
find the ip address of the current computer and then run the following command to add it.

```bash
./client <current computer ip>:9000 ADD_SERVER
```

---

**Note:**  
- All files are stored in the `Pi-Share` directory on the server.
- Replace `127.0.0.1` and port numbers with your actual server's IP and port as needed.

## Details
All files are downloaded to/shared from the `pi-sharing` directory.
This directory is automatically created if it doesn't exist.

## Team Contributions
- Manan - Handled the redirection on the client side when the server responded with a new server.
- Aryan - Implemented redirection of clients to other servers during GET and files from the client during PUT. 
- Jack - Implemented ADD_SERVER on both client and server side.

## Acknowledgments

This code was extended from the 341 networking MP.