/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ServerManager.cpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lsalin <lsalin@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2023/04/05 15:05:48 by lsalin            #+#    #+#             */
/*   Updated: 2023/07/11 13:32:51 by lsalin           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ServerManager.hpp"

ServerManager::ServerManager() {}
ServerManager::~ServerManager() {}

void	ServerManager::setupServers(std::vector<ServerConfig> servers)
{
	std::cout << std::endl;
	Logger::logMsg(LIGHTMAGENTA, CONSOLE_OUTPUT, "Initializing  Servers...");

	_servers = servers;

	char	buf[INET_ADDRSTRLEN];
	bool	serverDub;

	for (std::vector<ServerConfig>::iterator it = _servers.begin(); it != _servers.end(); ++it)
	{
		serverDub = false;

		for (std::vector<ServerConfig>::iterator it2 = _servers.begin(); it2 != it; ++it2)
		{
			if (it2->getHost() == it->getHost() && it2->getPort() == it->getPort())
			{
				it->setFd(it2->getFd());
				serverDub = true;
			}
		}

		if (!serverDub)
			it->setupServer();

		Logger::logMsg(LIGHTMAGENTA, CONSOLE_OUTPUT, "Server Created: ServerName[%s] Host[%s] Port[%d]",it->getServerName().c_str(),
				inet_ntop(AF_INET, &it->getHost(), buf, INET_ADDRSTRLEN), it->getPort());
	}
}

void	ServerManager::runServers()
{
	fd_set	recv_set_cpy;
	fd_set	write_set_cpy;
	int		select_ret;

	_biggest_fd = 0;
	initializeSets();
	
	struct timeval	timer;

	while (true)
	{
		timer.tv_sec = 1;
		timer.tv_usec = 0;
		recv_set_cpy = _recv_fd_pool;
		write_set_cpy = _write_fd_pool;

		if ((select_ret = select(_biggest_fd + 1, &recv_set_cpy, &write_set_cpy, NULL, &timer)) < 0 )
		{
			Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: select error %s   Closing ....", strerror(errno));
			exit(1);
			continue ;
		}

		for (int i = 0; i <= _biggest_fd; ++i)
		{
			if (FD_ISSET(i, &recv_set_cpy) && _servers_map.count(i))
				acceptNewConnection(_servers_map.find(i)->second);

			else if (FD_ISSET(i, &recv_set_cpy) && _clients_map.count(i))
				readRequest(i, _clients_map[i]);

			else if (FD_ISSET(i, &write_set_cpy) && _clients_map.count(i))
			{
				int cgi_state = _clients_map[i].response.getCgiState();
				
				if (cgi_state == 1 && FD_ISSET(_clients_map[i].response._cgi_obj.pipe_in[1], &write_set_cpy))
					sendCgiBody(_clients_map[i], _clients_map[i].response._cgi_obj);

				else if (cgi_state == 1 && FD_ISSET(_clients_map[i].response._cgi_obj.pipe_out[0], &recv_set_cpy))
					readCgiResponse(_clients_map[i], _clients_map[i].response._cgi_obj);

				else if ((cgi_state == 0 || cgi_state == 2)  && FD_ISSET(i, &write_set_cpy))
					sendResponse(i, _clients_map[i]);
			}
		}
		checkTimeout();
	}
}

void	ServerManager::checkTimeout()
{
	for (std::map<int, Client>::iterator it = _clients_map.begin() ; it != _clients_map.end(); ++it)
	{
		if (time(NULL) - it->second.getLastTime() > CONNECTION_TIMEOUT)
		{
			Logger::logMsg(YELLOW, CONSOLE_OUTPUT, "Client %d Timeout, Closing Connection..", it->first);
			closeConnection(it->first);
			return;
		}
	}
}

void	ServerManager::initializeSets()
{
	FD_ZERO(&_recv_fd_pool);
	FD_ZERO(&_write_fd_pool);

	for(std::vector<ServerConfig>::iterator it = _servers.begin(); it != _servers.end(); ++it)
	{
		if (listen(it->getFd(), 512) == -1)
		{
			Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: listen error: %s   Closing....", strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (fcntl(it->getFd(), F_SETFL, O_NONBLOCK) < 0)
		{
			Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: fcntl error: %s   Closing....", strerror(errno));
			exit(EXIT_FAILURE);
		}

		addToSet(it->getFd(), _recv_fd_pool);
		_servers_map.insert(std::make_pair(it->getFd(), *it));
	}
	
	_biggest_fd = _servers.back().getFd();
}


void	ServerManager::acceptNewConnection(ServerConfig &serv)
{
	struct sockaddr_in	client_address;
	long				client_address_size = sizeof(client_address);
	int					client_sock;
	Client				new_client(serv);
	char				buf[INET_ADDRSTRLEN];

	if ((client_sock = accept(serv.getFd(), (struct sockaddr *)&client_address,
	 (socklen_t*)&client_address_size)) == -1)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: accept error %s", strerror(errno));
		return;
	}

	Logger::logMsg(YELLOW, CONSOLE_OUTPUT, "New Connection From %s, Assigned Socket %d",inet_ntop(AF_INET, &client_address, buf, INET_ADDRSTRLEN), client_sock);

	addToSet(client_sock, _recv_fd_pool);

	if (fcntl(client_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: fcntl error %s", strerror(errno));
		removeFromSet(client_sock, _recv_fd_pool);
		close(client_sock);
		return;
	}

	new_client.setSocket(client_sock);

	if (_clients_map.count(client_sock) != 0)
		_clients_map.erase(client_sock);

	_clients_map.insert(std::make_pair(client_sock, new_client));
}

void	ServerManager::closeConnection(const int i)
{
	if (FD_ISSET(i, &_write_fd_pool))
		removeFromSet(i, _write_fd_pool);

	if (FD_ISSET(i, &_recv_fd_pool))
		removeFromSet(i, _recv_fd_pool);

	close(i);
	_clients_map.erase(i);
}

void	ServerManager::sendResponse(const int &i, Client &c)
{
	int			bytes_sent;
	std::string	response = c.response.getRes();

	if (response.length() >= MESSAGE_BUFFER)
		bytes_sent = write(i, response.c_str(), MESSAGE_BUFFER);
	else
		bytes_sent = write(i, response.c_str(), response.length());

	if (bytes_sent < 0)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "sendResponse(): error sending : %s", strerror(errno));
		closeConnection(i);
	}

	else if (bytes_sent == 0 || (size_t) bytes_sent == response.length())
	{
		Logger::logMsg(CYAN, CONSOLE_OUTPUT, "Response Sent To Socket %d, Stats=<%d>"
		, i, c.response.getCode());

		if (c.request.keepAlive() == false || c.request.errorCode() || c.response.getCgiState())
		{
			Logger::logMsg(YELLOW, CONSOLE_OUTPUT, "Client %d: Connection Closed.", i);
			closeConnection(i);
		}

		else
		{
			removeFromSet(i, _write_fd_pool);
			addToSet(i, _recv_fd_pool);
			c.clearClient();
		}
	}

	else
	{
		c.updateTime();
		c.response.cutRes(bytes_sent);
	}
}

void	ServerManager::assignServer(Client &c)
{
	for (std::vector<ServerConfig>::iterator it = _servers.begin(); it != _servers.end(); ++it)
	{
		if (c.server.getHost() == it->getHost() && c.server.getPort() == it->getPort()
			&& c.request.getServerName() == it->getServerName())
		{
			c.setServer(*it);
			return;
		}
	}
}

void	ServerManager::readRequest(const int &i, Client &c)
{
	char	buffer[MESSAGE_BUFFER];
	int		bytes_read = 0;

	bytes_read = read(i, buffer, MESSAGE_BUFFER);

	if (bytes_read == 0)
	{
		Logger::logMsg(YELLOW, CONSOLE_OUTPUT, "webserv: Client %d Closed Connection", i);
		closeConnection(i);
		return;
	}

	else if (bytes_read < 0)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "webserv: fd %d read error %s", i, strerror(errno));
		closeConnection(i);
		return;
	}

	else if (bytes_read != 0)
	{
		c.updateTime();
		c.request.feed(buffer, bytes_read);
		memset(buffer, 0, sizeof(buffer));
	}

	if (c.request.parsingCompleted() || c.request.errorCode())
	{
		assignServer(c);

		Logger::logMsg(CYAN, CONSOLE_OUTPUT, "Request Recived From Socket %d, Method=<%s>  URI=<%s>"
		, i, c.request.getMethodStr().c_str(), c.request.getPath().c_str());

		c.buildResponse();

		if (c.response.getCgiState())
		{
			handleReqBody(c);
			addToSet(c.response._cgi_obj.pipe_in[1], _write_fd_pool);
			addToSet(c.response._cgi_obj.pipe_out[0], _recv_fd_pool);
		}

		removeFromSet(i, _recv_fd_pool);
		addToSet(i, _write_fd_pool);
	}
}

void	ServerManager::handleReqBody(Client &c)
{
		if (c.request.getBody().length() == 0)
		{
			std::string			tmp;
			std::fstream		file;(c.response._cgi_obj.getCgiPath().c_str());
			std::stringstream	ss;

			ss << file.rdbuf();
			tmp = ss.str();
			c.request.setBody(tmp);
		}
}

void	ServerManager::sendCgiBody(Client &c, CgiHandler &cgi)
{
	int			bytes_sent;
	std::string	&req_body = c.request.getBody();

	if (req_body.length() == 0)
		bytes_sent = 0;

	else if (req_body.length() >= MESSAGE_BUFFER)
		bytes_sent = write(cgi.pipe_in[1], req_body.c_str(), MESSAGE_BUFFER);

	else
		bytes_sent = write(cgi.pipe_in[1], req_body.c_str(), req_body.length());

	if (bytes_sent < 0)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "sendCgiBody() Error Sending: %s", strerror(errno));
		removeFromSet(cgi.pipe_in[1], _write_fd_pool);
		close(cgi.pipe_in[1]);
		close(cgi.pipe_out[1]);
		c.response.setErrorResponse(500);
	}

	else if (bytes_sent == 0 || (size_t) bytes_sent == req_body.length())
	{
		removeFromSet(cgi.pipe_in[1], _write_fd_pool);
		close(cgi.pipe_in[1]);
		close(cgi.pipe_out[1]);
	}

	else
	{
		c.updateTime();
		req_body = req_body.substr(bytes_sent);
	}
}

void	ServerManager::readCgiResponse(Client &c, CgiHandler &cgi)
{
	char	buffer[MESSAGE_BUFFER * 2];
	int		bytes_read = 0;
	bytes_read = read(cgi.pipe_out[0], buffer, MESSAGE_BUFFER* 2);

	if (bytes_read == 0)
	{
		removeFromSet(cgi.pipe_out[0], _recv_fd_pool);
		close(cgi.pipe_in[0]);
		close(cgi.pipe_out[0]);

		int	status;
		waitpid(cgi.getCgiPid(), &status, 0);

		if (WEXITSTATUS(status) != 0)
			c.response.setErrorResponse(502);

		c.response.setCgiState(2);

		if (c.response._response_content.find("HTTP/1.1") == std::string::npos)
			c.response._response_content.insert(0, "HTTP/1.1 200 OK\r\n");

		return;
	}

	else if (bytes_read < 0)
	{
		Logger::logMsg(RED, CONSOLE_OUTPUT, "readCgiResponse() Error Reading From CGI Script: ", strerror(errno));
		
		removeFromSet(cgi.pipe_out[0], _recv_fd_pool);
		close(cgi.pipe_in[0]);
		close(cgi.pipe_out[0]);

		c.response.setCgiState(2);
		c.response.setErrorResponse(500);

		return;
	}

	else
	{
		c.updateTime();
		c.response._response_content.append(buffer, bytes_read);
		memset(buffer, 0, sizeof(buffer));
	}
}

void	ServerManager::addToSet(const int i, fd_set &set)
{
	FD_SET(i, &set);

	if (i > _biggest_fd)
		_biggest_fd = i;
}

void	ServerManager::removeFromSet(const int i, fd_set &set)
{
	FD_CLR(i, &set);

	if (i == _biggest_fd)
		_biggest_fd--;
}