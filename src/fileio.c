#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#include <linux/limits.h>

#include <texted/fileio.h>
#include <texted/insert.h>
#include <texted/texted.h>

volatile int temp = 0;

void set_temp() { temp = 1; }
void clr_temp() { temp = 0; }
int get_temp()  { return temp; }

// If temp is set read from /tmp
// Else read from filename
// On error errno is set

char* loadFile(char* Filename)
{
	FILE* File;
	ssize_t FileSize = 0;
	char* Buffer = NULL;

	if(get_temp())
		Filename = TMP_PATH;

	// Check file size
	FileSize = getFileSize(Filename);
	if(FileSize == 0) return NULL;

	// Allocate FileSize space for the Buffer
	Buffer = malloc(FileSize * sizeof(char) + 1);
	// Valgrind wants me to initialize the Buffer
	empty(Buffer, FileSize * sizeof(char) + 1);

	// Defensive
	if(!Buffer) {
		fputs(RED "DIE\n" RESET, stderr);
		exit(1);
	}

	// Open file
	File = fopen(Filename, "r");
	if (!File) {
		// If there is no file create it
		if(createFile() == ED_NULL_FILE_PTR) {
			if(Buffer)
				free(Buffer);
			return NULL;
		}
	}

	// If file is empty exit with null
	if(!Buffer) {
		fclose(File);
		return Buffer;
	}

	// Read the whole file
	ssize_t n;
	if((n = fread(Buffer, sizeof(char), FileSize, File)) != FileSize) {
		fprintf(stderr, RED "Error on read file(%ld): %s\n" RESET, n, strerror(errno));
		exit(1);
	}

	fclose(File);
	return Buffer;
}

ssize_t getFileSize(char* Filename)
{
	struct stat st;
	return st.st_size *= ((stat(Filename, &st) + 1) != 0);
}

// Saves the Buffer in the File and frees the Buffer
int app_save(char* Filename, char* Buffer)
{
	FILE* File;

	if(!Buffer)
		return ED_NULL_PTR;

	File = fopen(Filename, "a");
	if (!File)
		return ED_NULL_FILE_PTR;

	fprintf(File, "%s", Buffer);
	fclose(File);

	clr_temp();
	return ED_SUCCESS;
}

int save(char* Filename, char* Buffer)
{
	FILE* File;

	File = fopen(Filename, "w");
	if (!File)
		return ED_NULL_FILE_PTR;

	if(Buffer)
		fputs(Buffer, File);
	fclose(File);

	if(get_temp() && strcmp(Filename, TMP_PATH)) {
		fputs(ITALIC CYAN "New file created: " RESET, stderr);
		fputs(Filename, stderr);
		fputc('\n', stderr);
		clr_temp();
	}

	return ED_SUCCESS;
}

char* genBackupName(char* Filename)
{
	char* BackupName;

	BackupName = strins(Filename, "-bkp", '.');
	if(!BackupName) {
		BackupName = malloc(strlen(Filename) + 5);
		strcpy(BackupName, Filename);
		strcat(BackupName, "-bkp");
	}

	return BackupName;
}

int backup(char* Filename)
{
	FILE* From, *To;
	char Buffer[LINE_SIZE];
	char* BackupName;

	if(get_temp())
		return ED_NULL_FILE_PTR;

	BackupName = genBackupName(Filename);

	From = fopen(Filename, "r");
	if (!From) {
		free(BackupName);
		return ED_NULL_FILE_PTR;
	}
	
	To = fopen(BackupName, "w");
	if (!To) {
		fclose(From);
		free(BackupName);
		return ED_NULL_FILE_PTR;
	}

	while (fgets(Buffer, LINE_SIZE, From))
		fprintf(To, "%s", Buffer);

	fclose(From);
	fclose(To);
	free(BackupName);
	return ED_SUCCESS;
}

LineBuffer_s* lbLoadFile(char* Filename)
{
	char* Buffer;
	LineBuffer_s* LineBuffer;

	Buffer = loadFile(Filename);
	if(!Buffer)
		return NULL;
	
	LineBuffer = getLineBuffer(Buffer);

	free(Buffer);
	return LineBuffer;
}

static void path_kill_back(char* path){
	size_t len = strlen(path);
	if( len == 1 ) return;
	if( path[len-1] == '/' ) path[len-1] = 0;
	char* bs = strrchr(path, '/');
	if( bs ) *bs = 0;
}

static int path_home(char* path){
    char *hd;
    if( (hd = getenv("HOME")) == NULL ){
		struct passwd* spwd = getpwuid(getuid());
		if( !spwd ){
			*path = 0;
			return -1;
        }
        strcpy(path, spwd->pw_dir);
    }
	else{
		strcpy(path, hd);
	}
	return 0;
}

char* path_resolve(const char* path){
	char cur[PATH_MAX];
	char out[PATH_MAX];

	size_t lpath = strlen(path);
	if( lpath > PATH_MAX - 1 ) return NULL;

	if( (lpath == 1 && !strncmp(path, "~", 1)) || !strncmp(path, "~/", 2) ){
		if( path_home(cur) ) return NULL;
		if( lpath + strlen(cur) > PATH_MAX -1 ) return NULL;
		if( path[1] && path[2] ){
			strcpy(&cur[strlen(cur)], &path[1]);
		}
	}
	else if( (lpath == 1 && !strncmp(path, ".", 1)) || !strncmp(path, "./", 2) ){
		getcwd(cur, PATH_MAX);
		if( lpath + strlen(cur) > PATH_MAX - 1) return NULL;
		if( path[1] && path[2] ){
			strcpy(&cur[strlen(cur)], &path[1]);
		}
	}
	else if( (lpath ==  2 && !strncmp(path, "..", 2)) || !strncmp(path, "../", 3) ){
		getcwd(cur, PATH_MAX);
		path_kill_back(cur);
		if( lpath + strlen(cur) > PATH_MAX - 1) return NULL;
		if( path[2] && path[3] ){
			strcpy(&cur[strlen(cur)], &path[2]);
		}
	}
	else{
		strcpy(cur, path);
	}

	char* parse = cur;
	char* pout = out;
	while( *parse ){
		if( *parse == '.' ){
			if( *(parse+1) == '/' ){
				parse += 2;
				continue;
			}
			if( *(parse+1) == '.' ){
				if( *(parse+2) == 0 ){
					*pout = 0;
					path_kill_back(out);
					pout = out + strlen(out);
					parse += 2;
					continue;
				}
				if ( *(parse+2) == '/' ){
					*pout = 0;
					path_kill_back(out);
					pout = out + strlen(out);
					parse += 2;
					continue;
				}
			}
		}
		*pout++ = *parse++;
	}
	*pout = 0;
	return strdup(out);
}


