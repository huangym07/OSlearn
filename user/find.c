#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define MYBUFSIZ 512

char *fmtname(char *path)
{
	char *p;
	for (p = path + strlen(path); p >= path && *p != '/'; p--)
		;
	p++;

	return p;
}

void find(char *path, char *target)
{
	int fd;
	struct stat st;
	if ((fd = open(path, 0)) < 0) {
		fprintf(2, "find: cannot open %s\n", path);
		return;
	}
	if (fstat(fd, &st) < 0) {
		fprintf(2, "find: cannot stat %s\n", path);
		close(fd);
		return;
	}

	struct dirent de;
	if (st.type == T_FILE) {
		if (strcmp(fmtname(path), target) == 0) {
			printf("%s\n", path);
			return;
		}
	} else if (st.type == T_DIR) {
		if (strlen(path) + 1 + DIRSIZ + 1 > MYBUFSIZ) {
			fprintf(2, "find: path too long\n");
			close(fd);
			return;
		}
		char *p = path + strlen(path);
		*p++ = '/';
		while (read(fd, &de, sizeof(de))) {
			if (de.inum == 0) continue;
			// printf("name is %s, and its inum is %d\n", de.name, de.inum);
			if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
			memmove(p, de.name, DIRSIZ);
			p[DIRSIZ] = 0;
			find (path, target);
		}
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		fprintf(2, "usage: find <pathname> <target_file_name>\n");
		exit(1);
	}
	char buf[MYBUFSIZ];
	strcpy(buf, argv[1]);
	find(buf, argv[2]);

	exit(0);
}