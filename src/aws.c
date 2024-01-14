// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

// static http_parser request_parser;
// static char request_path[BUFSIZ];	/* storage for request_path */
int connection_send_dynamic(struct connection *conn);

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	// printf("aaaaaaaaaaaaaaaaa\n");

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	// strcat(conn->send_buffer, "HTTP/1.1 200 OK\r\n\r\n");
	// strcat(conn->send_buffer, "HTTP/1.1 200 OK\r\n"
	// 							"Content-Length: %d\r\n"
	// 							"Content-Type: text/plain\r\n\r\n" );
	sprintf(conn->send_buffer, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/plain\r\n\r\n", (int) conn->file_size);
	
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	strcat(conn->send_buffer, "HTTP/1.1 404 Not Found\r\n\r\n");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->request_path, "static")) {
		strcpy(conn->filename, AWS_ABS_STATIC_FOLDER);
		conn->res_type = RESOURCE_TYPE_STATIC;
		printf("Static filename is @%s@\n", conn->filename);
		return RESOURCE_TYPE_STATIC;
	}
	if (strstr(conn->request_path, "dynamic")) {
		strcpy(conn->filename, AWS_ABS_DYNAMIC_FOLDER);
		conn->res_type = RESOURCE_TYPE_DYNAMIC;
		printf("DYNAMIC filename is @%s@\n", conn->filename);
		return RESOURCE_TYPE_DYNAMIC;
	}
	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *connection = malloc(sizeof(*connection));

	connection->sockfd = sockfd;
	memset(connection->recv_buffer, 0, BUFSIZ);
	memset(connection->send_buffer, 0, BUFSIZ);

	return connection;
	return NULL;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	
	memset(&conn->iocb, 0, sizeof(struct iocb));

	io_prep_pread(&conn->iocb, conn->fd, conn->recv_buffer, conn->recv_len, 0);

	conn->piocb[0] = &conn->iocb;

	if (io_submit(ctx, 1024, conn->piocb) < 0) { //prev it was 1024 instead of 1  {
		dlog(LOG_ERR, "IO SUBMIT ERROR\n");
	}
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	w_epoll_remove_fd(epollfd, conn->sockfd);
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */

	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;

	/* TODO: Accept new connection. */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	
	/* TODO: Set socket to be non-blocking. */
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

	/* TODO: Instantiate new connection handler. */
	conn = connection_create(sockfd);
	
	/* TODO: Add socket to epoll. */
	w_epoll_add_ptr_in(epollfd, sockfd, conn);

	/* TODO: Initialize HTTP_REQUEST parser. */
	// http_parser_init(&request_parser, HTTP_REQUEST);
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	
	int bytes_received = 0;
	conn->recv_len = 0;

	dlog(LOG_DEBUG, "Entered receive_data funnnnnnnnnnnncccccccccc\n");

	while (1){

		bytes_received = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ, 0);

		if (bytes_received < 0) {
			// printf("ERROR\n");
			dlog(LOG_DEBUG, "ERROR in revc bytes recv < 0\n");
			return;
		} else if (bytes_received == 0) {
			// printf("Conn closed\n");
			dlog (LOG_DEBUG, "Conn closed \n");
			return;
		}
		conn->recv_len += (size_t) bytes_received;
		conn->recv_buffer[(int) conn->recv_len] = '\0';
		dlog(LOG_DEBUG, "After another recv call, recv buffer is : ##%s#@ and len is %d\n", conn->recv_buffer, (int) conn->recv_len);

		if (strstr(conn->recv_buffer, "\r\n\r\n")) {
			dlog (LOG_DEBUG, "Break if statement for recv buff = @%s@\n", conn->recv_buffer);
			break;
		}
	}

	// http_parser_execute(&request_parser, &http_parser_settings, conn->recv_buffer, conn->recv_len);
	conn->recv_buffer[conn->recv_len] = '\0';
	conn->state = STATE_REQUEST_RECEIVED;
	dlog(LOG_DEBUG, "recv buffer is : %s@\n", conn->recv_buffer);
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	// int fd = open(conn->filename, O_RDONLY);
	int fd = open(conn->request_path, O_WRONLY | O_CREAT | O_TRUNC);

	if (fd < 0) {
		printf("error opening file\n");
		return -1;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		printf("Error at openfile for file stats\n");
		return -1;
	}

	conn->file_size = st.st_size;
	conn->fd = fd;
	conn->file_pos = 0;

	dlog (LOG_DEBUG, "In functia open_file, fd = %d si conn.reqPath = @%s@\n", conn->fd, conn->request_path);

	return fd;

	return -1;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */

	struct io_event events[1];

    int num_events = io_getevents(ctx, 1, 1, events, NULL);

    if (num_events < 0) {
        return;
    }

    // Check if the operation completed successfully
    if (events[0].res < 0) {
        return;
    }

	//daca am ajuns aici inseamna ca operatia s a terminat cu succes deci ma pregatesc sa trimit pe socket
	// connection_send_data(conn);
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};
	// http_parser_init(&conn->request_parser, HTTP_REQUEST);
	conn->request_parser.data = conn;
	http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, conn->recv_len);
	// http_parser_execute(&conn->request_parser, &settings_on_path, &conn, sizeof(struct connection));
	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	int file_descriptor = connection_open_file(conn);

	if (file_descriptor < 0 ) {
		printf("Error inside connection_send_static with file descriptor when opening file\n");
		return STATE_CONNECTION_CLOSED;
	}

	// connection_prepare_send_reply_header(conn);

	// ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

	// if (sent_header_size < 0) {
	// 	printf ("Error when sending the header in static send\n");
	// 	close(file_descriptor);
	// 	return STATE_CONNECTION_CLOSED;
	// }

	// int initial_response_header_size = strlen(conn->send_buffer);





	off_t offset = 0;
	ssize_t remaining = conn->file_size;
	dlog (LOG_DEBUG, "Initially, remaining is : %d\n", (int) remaining);

	while ((int) remaining > 0) {
		ssize_t sent = sendfile(conn->sockfd, file_descriptor, &offset, remaining);

		if (sent < 0) {
			dlog (LOG_ERR, "Error sending static\n");
			close(file_descriptor);
			return STATE_CONNECTION_CLOSED;
		}

		dlog (LOG_DEBUG, "inside send_static while loop have been sent %d bytes and offset is %d\n", (int) sent, (int)  offset);
		dlog (LOG_DEBUG, "There are %d bytes remaining to be sent \n\n", (int) remaining);

		// offset = offset + sent;
		remaining = remaining - sent;
	}

	// sendfile(conn->sockfd, file_descriptor, 0, conn->file_size);
	
	// char end_line[] = "\r\n";
	// ssize_t sent_endline = send(conn->sockfd, end_line, sizeof(end_line), 0);

	// if (sent_endline < 0) {
	// 	printf("Error sending additional data\n");
	// 	close(file_descriptor);
	// 	return STATE_CONNECTION_CLOSED;
	// }

	close (file_descriptor);
	dlog (LOG_DEBUG, "Sendfile static send successfully\n");

	// w_epoll_remove_fd(epollfd, conn->sockfd);

	return STATE_CONNECTION_CLOSED;

	return STATE_NO_STATE;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	printf ("inside conn_send_data conn_have_path = %d\n", conn->have_path);
	if (conn->have_path == 0) {
		//trebuie sa intoarcem response 404;
		printf ("must send 404 inside conn_send_data\n");
		connection_prepare_send_404(conn);

		ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

		if (sent_header_size < 0) {
			printf ("Error when sending the header 404 in conn_send_data\n");
			w_epoll_remove_fd(epollfd, conn->sockfd);
			close(conn->sockfd);
			return -1; //STATE_CONNECTION_CLOSED;
		}
		conn->state = STATE_404_SENT;

		// w_epoll_remove_fd(epollfd, conn->sockfd);
		// close(conn->sockfd);
		// conn->state = STATE_CONNECTION_CLOSED;
		return sent_header_size;
	} else {
		//trimitem frumos mai intai header apoi vedem daca e static sau dinamic
		
		
		connection_prepare_send_reply_header(conn);

		ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

		if (sent_header_size < 0) {
			printf ("Error when sending the header in conn_send_data\n");
			return -1;
		}

		if (conn->res_type == RESOURCE_TYPE_STATIC) {
			printf ("va trebui sa fac static send cu SENDIFLE\n");
			connection_send_static(conn);
			conn->state = STATE_CONNECTION_CLOSED;
			dlog (LOG_DEBUG, "conn_send_data, am iesit din send_static si conn status este : %d\n", conn->state);
		} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
			printf ("va trebui sa trimit dynamic\n");
			connection_send_dynamic(conn);
		}

	}
	return -1;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	return 0;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */
	printf("handle input conn state is %d\n", conn->state);
	dlog(LOG_DEBUG, "handle input conn state is %d aa (at the beginning of handle_input)\n", conn->state);

	switch (conn->state) {
	case STATE_RECEIVING_DATA:
		receive_data(conn);
		printf("Before parse_header @@%s@@ and recv_buff is : @%s@\n", conn->request_path, conn->recv_buffer);
		parse_header(conn);
		printf("After parse_header @@%s@@\n", conn->request_path);
		
		conn->state = STATE_REQUEST_RECEIVED;

		struct epoll_event ev;
		ev.events = EPOLLOUT; // inainte era EPOLLIN | EPOLLOUT
		ev.data.ptr = conn;
		epoll_ctl(epollfd, EPOLL_CTL_MOD, conn->sockfd, &ev);

		enum resource_type resource_type_conn =  connection_get_resource_type(conn);
		conn->res_type = resource_type_conn;

		//test-start
		char *newPath = (char *)malloc( strlen(conn->request_path) + 1);
		newPath[0] = '.';
		if (!(conn->request_path[0] == '.' && conn->request_path[1] != '.')){
			strcpy(newPath + 1, conn->request_path);
		}
		printf("MY NEW PATH: ##%s##\n", newPath);
		strcpy(conn->request_path, newPath); //ii adaug un . inainte la conn.request->path
		if (access(newPath, F_OK) != -1) {
			//exista fisierul este ok
			printf ("Fisierul %s DA DA exista\n", newPath);
			conn->have_path = 1;
			free(newPath);
		} else {
			// nu exista fisierul send 404
			printf ("Fisierul %s NU exista 404\n", newPath);
			free(newPath);
			conn->have_path = 0;
			//send 404 here and return

			// connection_prepare_send_404(conn);

			// ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

			// if (sent_header_size < 0) {
			// 	printf ("Error when sending the header 404 in static send\n");
			// 	w_epoll_remove_fd(epollfd, conn->sockfd);
			// 	close(conn->sockfd);
			// 	return; //STATE_CONNECTION_CLOSED;
			// }

			// // w_epoll_remove_fd(epollfd, conn->sockfd);
			// // close(conn->sockfd);
			// return;
		}
		// sleep(30);
		//test end
		// if (resource_type_conn == RESOURCE_TYPE_STATIC) {
		// 	printf ("Manage static...\n");
		// 	connection_send_static(conn);
		// } else if (resource_type_conn == RESOURCE_TYPE_DYNAMIC) {
		// 	printf ("Manage dynamic...\n");
		// 	connection_send_dynamic(conn);
		// } else if (resource_type_conn == RESOURCE_TYPE_NONE) {
		// 	//send 404??
		// }
		// w_epoll_remove_fd(epollfd, conn->sockfd);
		// close(conn->sockfd);
		//once data is received, it is time to send it
		break;
	default:
		printf("shouldn't get here %d\n", conn->state);
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */
	printf("handle output conn state is %d\n", conn->state);
	dlog(LOG_DEBUG, "handle output conn state is %d\n", conn->state);

	switch (conn->state) {
	case STATE_REQUEST_RECEIVED:
		connection_send_data(conn);

	break;
	default:
		ERR("Unexpected state\n");
		exit(1);
	}

}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (event & EPOLLIN) {
		dlog(LOG_DEBUG, "New message\n");
		conn->state = STATE_RECEIVING_DATA;
		handle_input(conn);
	}
	// printf ("Handle-client done with EPOLLIN, maybe time to EPOLLOUT?\n");
	if (event & EPOLLOUT) {
		dlog(LOG_DEBUG, "Ready to send message\n");
		// printf ("INSIDE OOOOOOOOOOOOOOOOOOOOUUUUUUUUUUUUUUUUUUUUUUTTTTTTTTTTTTTTTTTTTTTTTT\n");
		handle_output(conn);
		conn->state = STATE_DATA_SENT;
		printf ("Conn status is ------------------->>>>>>>>> OUT : %d\n", conn->state);
	}
	
	if (conn->state == STATE_CONNECTION_CLOSED) {
		printf ("AM INTRAT AICI $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n\n");
		dlog (LOG_DEBUG, "Enteren handle client with conn status closed (check : con status = %d )\n", conn->state);
		w_epoll_remove_fd(epollfd, conn->sockfd);
		close(conn->sockfd);
	}

	// if (conn->state == STATE_404_SENT) {
	// 	printf ("404 sent now time to close connection\n");
	// 	// w_epoll_remove_fd(epollfd, conn->sockfd);
    //     // close(conn->sockfd);
    //     // conn->state = STATE_CONNECTION_CLOSED; 
	// }
	if (conn->state == STATE_DATA_SENT) {
		dlog (LOG_DEBUG, "Enteren handle client with conn status DATA SENT si il fac CLOSED (check : con status = %d )\n", conn->state);
		conn->state = STATE_CONNECTION_CLOSED;
		w_epoll_remove_fd(epollfd, conn->sockfd);
		connection_remove(conn);
	}
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */
	rc = io_setup(1024, &ctx); // prev it was 1024 instead of 1

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);

	/* TODO: Add server socket to epoll object*/
	w_epoll_add_fd_in(epollfd, listenfd);

	/* Uncomment the following line for debugging. */
	// dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		w_epoll_wait_infinite(epollfd, &rev);
		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		// if (rev.data.fd == listenfd) {
		// 	if (rev.events & EPOLLIN) {
		// 		// dlog(LOG_DEBUG, "New connection\n");
		// 		handle_new_connection();
		// 	}
		// } else {
		// 	if (rev.events & EPOLLIN) {
		// 		// dlog(LOG_DEBUG, "New message\n");
		// 		handle_client_request(rev.data.ptr);
		// 	}
		// 	if (rev.events & EPOLLOUT) {
		// 		// dlog(LOG_DEBUG, "Ready to send message\n");
		// 		send_message(rev.data.ptr);
		// 	}
		// }
		if (rev.events == EPOLLIN && rev.data.fd == listenfd) {
			handle_new_connection();
		} else if (rev.data.fd != listenfd) {
			struct connection *conn = rev.data.ptr;
			handle_client(rev.events, conn);
			// if (rev.events & (EPOLLHUP | EPOLLRDHUP)) {
			// 	connection_remove(conn);
			// }
		}
	}

	close(listenfd);
	io_destroy(ctx);

	return 0;
}
