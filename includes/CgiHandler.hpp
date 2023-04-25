/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lsalin <lsalin@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2023/04/07 11:00:47 by lsalin            #+#    #+#             */
/*   Updated: 2023/04/25 14:10:48 by lsalin           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "Webserv.hpp"

class HttpRequest;

// Gere les appels aux scripts CGI

class CgiHandler
{
	private:
		std::map<std::string, std::string>	_env;
		char								**_ch_env;
		char								**_argv;
		int									_exit_status;
		std::string							_cgi_path;
		pid_t								_cgi_pid;

	public:

		// Le script CGI lit les données envoyées par le serveur dans pipe_in[0]
		// Et écrit ses résultats dans pipe_out[1]
		// Le serveur lit les données du script dans pipe_out[0]
		// Et écrit dans pipe_in[1]

		int	pipe_in[2];
		int	pipe_out[2];

		CgiHandler();
		CgiHandler(std::string path);
		~CgiHandler();
		CgiHandler(CgiHandler const &other);
		CgiHandler	&operator=(CgiHandler const &rhs);

		void		initEnv(HttpRequest& req, const std::vector<Location>::iterator it_loc);
		void		initEnvCgi(HttpRequest& req, const std::vector<Location>::iterator it_loc);
		void 		execute(short &error_code);
		void 		sendHeaderBody(int &pipe_out, int &fd, std::string &);
		void 		fixHeader(std::string &header);
		void		clear();

		std::string setCookie(const std::string& str);

		void 		setCgiPid(pid_t cgi_pid);
		void		setCgiPath(const std::string &cgi_path);

		const std::map<std::string, std::string>	&getEnv() const;
		const pid_t									&getCgiPid() const;
		const std::string							&getCgiPath() const;

		std::string	getAfter(const std::string& path, char delim);
		std::string	getBefore(const std::string& path, char delim);
		std::string	getPathInfo(std::string& path, std::vector<std::string> extensions);
		int			countCookies(const std::string& str);
		int			findStart(const std::string path, const std::string delim);
		std::string	decode(std::string &path);
};

#endif