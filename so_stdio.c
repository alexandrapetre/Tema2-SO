#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "so_stdio.h"
#define BUFSIZE (4096)

struct _so_file {

	char *buffer;
	int fd;
	int append;
	int count;
	int index;
	int read;
	int totalBytesRead;
	int lastWrite;
	int pid;
	int err;
};

typedef struct _so_file SO_FILE;

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *file = NULL;

	file = malloc(sizeof(SO_FILE));
	if (file == NULL)
		return NULL;

	file->count = 0;
	file->index = 0;
	file->read = 0;
	file->lastWrite = 0;
	file->err = 0;
	file->fd = 0;
	file->totalBytesRead = 0;
	file->pid = -1;

	file->buffer = calloc(BUFSIZE, sizeof(char));

	if (file->buffer == NULL) {
		free(file);
		file = NULL;
	}

	if (file != NULL) {
		if (strcmp(mode, "r") == 0) {
			file->append = 0;
			file->fd = open(pathname, O_RDONLY, 0644);
		} else if (strcmp(mode, "w") == 0) {
			file->append = 0;
			file->fd = open(pathname, O_WRONLY | O_CREAT |
				O_TRUNC, 0644);
		} else if (strcmp(mode, "w+") == 0) {
			file->append = 0;
			file->fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC,
				0644);
		} else if (strcmp(mode, "r+") == 0) {
			file->fd = open(pathname, O_RDWR, 0644);
			file->append = 0;
		} else if (strcmp(mode, "a") == 0) {
			file->fd = open(pathname, O_WRONLY | O_CREAT |
				O_APPEND, 0644);
			file->append = 1;
		} else if (strcmp(mode, "a+") == 0) {
			file->append = 1;
			file->fd = open(pathname, O_RDWR | O_CREAT |
				O_APPEND, 0644);
		} else {
			free(file->buffer);
			free(file);
			file = NULL;
		}

		if (file != NULL && file->fd < 0) {
			free(file->buffer);
			free(file);
			file = NULL;
		}
	}


	return file;
}

int so_fgetc(SO_FILE *stream)
{       int elem = SO_EOF;
	int bytesRead = 0;
	int verify = 1;

	if (stream == NULL) {
		stream->err = 1;
		return SO_EOF;
	}

	if (stream->index >= stream->read) {
		stream->index = 0;
		stream->read = 0;
		verify = 0;
	}

	if (verify == 0) {
		bytesRead = read(stream->fd, stream->buffer, BUFSIZE);
		if (bytesRead <= 0 || bytesRead > BUFSIZE) {
			stream->err = 1;
			stream->lastWrite = 2;
			return SO_EOF;
		}
		elem = (unsigned char)(stream->buffer[stream->index]);
		stream->index += 1;
		stream->read = bytesRead;
		stream->totalBytesRead = bytesRead;
	} else {
		elem = (unsigned char)(stream->buffer[stream->index]);
		stream->index += 1;
	}

	stream->lastWrite = 2;
	return elem;
}

int so_fputc(int c, SO_FILE *stream)
{
	stream->lastWrite = 1;

	if (stream == NULL) {
		stream->err = 1;
		return SO_EOF;
	}

	if (stream->index == BUFSIZE) {
		stream->totalBytesRead = stream->index;
		if (so_fflush(stream) == SO_EOF) {
			stream->err = 1;
			return SO_EOF;
		}
	}

	stream->buffer[stream->index] = c;
	stream->index += 1;

	return c;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytesRead = nmemb * size;
	int elem;
	int i;

	if (stream->index >= BUFSIZE || stream == NULL)
		return 0;

	if (stream != NULL) {
		for (i = 0; i < bytesRead; i++) {
			elem = so_fgetc(stream);

			if (elem >= 0)
				(*(char *)ptr) = elem;
			else
				return i;
			ptr++;
		}
	}

	return nmemb;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytesWritten = size * nmemb;
	int i;
	int elem;
	int fputs;

	if (stream != NULL) {
		for (i = 0; i < bytesWritten; i++) {
			elem = (*(char *)ptr);
			fputs = so_fputc(elem, stream);
			if (fputs < 0 && elem >= 0)
				return i;
			ptr++;
		}
	}

	return nmemb;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int fseek = -1;
	int fflush;

	if (stream == NULL)
		return SO_EOF;

	fflush = so_fflush(stream);

	if (fflush < 0) {
		stream->err = 1;
		return SO_EOF;
	}

	if (whence == SEEK_END || whence == SEEK_CUR || whence == SEEK_SET)
		fseek = lseek(stream->fd, offset, whence);

	if (fseek < 0) {
		stream->err = 1;
		return SO_EOF;
	}

	stream->totalBytesRead = fseek;

	return 0;
}

SO_FILE *so_popen(const char *command, const char *type)
{	
	SO_FILE *file = NULL;
	int fd[2];
	int fdChild = 0;
	int fdParent = 0;
	pid_t pid;

	if (pipe(fd) == -1)
		return NULL;

	pid = fork();

	switch (pid) {
		case -1: {
			close(fd[0]);
			close(fd[1]);
			file = NULL;
			break;
		}

		case 0: {

			if (strcmp(type, "r") == 0) {
				fdParent = STDIN_FILENO;
				fdChild = STDOUT_FILENO;
			} else if (strcmp(type, "w") == 0) {
				fdParent = STDOUT_FILENO;
				fdChild = STDIN_FILENO;
			}
			close(fd[fdParent]);
			dup2(fd[fdChild], fdChild);
			execlp("/bin/sh", "sh", "-c", command, NULL);
			break;
		}

		default: {

			if (strcmp(type, "r") == 0) {
				fdParent = STDIN_FILENO;
				fdChild = STDOUT_FILENO;
			} else if (strcmp(type, "w") == 0) {
				fdParent = STDOUT_FILENO;
				fdChild = STDIN_FILENO;
			}

			close(fd[fdChild]);

			file = malloc(sizeof(SO_FILE));
			if (file == NULL) {
				close(fd[0]);
				close(fd[1]);
				file = NULL;
			}

			if (file != NULL) {

				file->buffer = calloc(BUFSIZE, sizeof(char));
				if (file->buffer == NULL) {
					free(file);
					close(fd[0]);
					close(fd[1]);
					file = NULL;
				}

				file->fd = fd[fdParent];
				file->count = 0;
				file->index = 0;
				file->read = 0;
				file->lastWrite = 0;
				file->err = 0;
				file->totalBytesRead = 0;
				file->pid = pid;
			}
			break;
		}
	}

	return file;
}

int so_pclose(SO_FILE *stream)
{
	int fflush = SO_EOF;
	int pid = stream->pid;
	int status;
	int wait = -1;

	fflush = so_fflush(stream);

	if (fflush == SO_EOF) {
		close(stream->fd);
		free(stream->buffer);
		free(stream);
		wait = waitpid(pid, &status, 0);

		return SO_EOF;
	}

	free(stream->buffer);
	close(stream->fd);
	free(stream);
	wait = waitpid(pid, &status, 0);

	if (wait < 0)
		return SO_EOF;
	return 0;
}

int so_fflush(SO_FILE *stream)
{
	int bytesWritten = 0;
	int numberBytes = 0;
	int lastBytes = 0;

	if (stream->lastWrite != 1) {
		memset(stream->buffer, 0, BUFSIZE);
		stream->index = 0;
		stream->read = 0;
		return 0;
	}

	if (stream != NULL && stream->index != 0) {
		if (stream->lastWrite == 1) {
			numberBytes = stream->index;
			if (numberBytes < 0)
				return SO_EOF;
			while (numberBytes > 0) {
				bytesWritten = write(stream->fd,
					stream->buffer + lastBytes,
					numberBytes);
				if (bytesWritten < 0) {
					stream->err = 1;
					return SO_EOF;
				}
				numberBytes -= bytesWritten;
				lastBytes += bytesWritten;
			}
			memset(stream->buffer, 0, BUFSIZE);
			stream->index = 0;
			stream->read = 0;
		}
	}

	return 0;
}

int so_fclose(SO_FILE *stream)
{
	int fflush = so_fflush(stream);

	if (close(stream->fd) < 0) {
		free(stream->buffer);
		free(stream);
		stream = NULL;
		return SO_EOF;
	}

	free(stream->buffer);
	free(stream);
	stream = NULL;

	if (fflush == SO_EOF)
		return SO_EOF;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	return stream->totalBytesRead + stream->index;
}

int so_feof(SO_FILE *stream)
{
	return stream->err;
}

int so_ferror(SO_FILE *stream)
{
	return stream->err;
}
