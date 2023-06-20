# Senior-thesis-project
This is a code for an HTTP server implemented in C. It provides functionality for handling HTTP requests and generating HTTP responses. The server is designed to be multithreaded, utilizing a thread pool for efficient request handling.

## Usage:
* Include the http.h header file in your project.
* Implement the desired HTTP request handler function.
* Initialize the thread pool with the desired number of threads and the handler function.
* Call the listen_and_serve function, providing the desired port number to listen on.
* Start the server and handle incoming requests.

## Example  
```
#include "main.h"
void post_handler(http_request_t *request, http_response_t *response)
{
    write_response(404, "text/plain", "Hello, world!", response);
}

int main()
{
    // Define the port to listen on
    char *port = "8090";

    // Start the server and listen for incoming requests
    listen_and_serve(port, 20, post_handler);

    return 0;
}
```
