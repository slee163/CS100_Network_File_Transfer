#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/sendfile.h>
#include <string.h>
#include "Timer.h"
#include <pthread.h>
#include <sstream>


using namespace std;

char * files[] = {
"/usr/share/dict/words",
"/usr/include/asm-x86_64/unistd.h",
"/usr/include/xulrunner-17.0.6/nsIDOMHTMLTextAreaElement.h",
"/usr/include/xulrunner-17.0.6/gfxContext.h",
"/usr/include/boost/test/test_tools.hpp",
"/usr/include/gssapi/gssapi.h",
"/usr/include/xf86drm.h",
"/usr/include/kde/ksocketaddress.h",
"/usr/include/kde/kpropertiesdialog.h",
"/usr/include/c++/4.1.1/bits/stl_function.h",
"/usr/include/linux/ixjuser.h",
"/usr/include/wx-2.8/wx/html/htmlcell.h",
"/usr/include/wx-2.8/wx/gdicmn.h",
"/usr/include/SDL/SDL_mixer.h"
};
#define files_length() (sizeof files / sizeof files[0])
#define THREAD_PORT 18681
#define MAX_THREADS 10

struct threadArgs
{
    int threadNum;
    char hostname[256];
    char filename[256];
};

string intToString(int num)
{
    string numString;
    ostringstream converter;
    converter << num;
    numString = converter.str();
    return numString;
}

void errcheck(int result, string msg, bool fatal)
{
    if(result == -1)
    {
        perror(msg.c_str());
        if(fatal)
        {
            exit(-1);
        }
    }
}

void setupHostent(hostent*& host, const char* host_name)
{
    memset(&host,0,sizeof(host));
    bzero(&host,sizeof(host));
    host = gethostbyname(host_name);
    if(host == NULL)
    {errcheck(-1,"Error getting hostent", true);}
    
}

void setupSockaddr(sockaddr_in& sock_address, hostent* host)
{
    memset(&sock_address,0,sizeof(sock_address));
    bzero(&sock_address,sizeof(sock_address));
    
    sock_address.sin_family = AF_INET;
    sock_address.sin_port = htons(THREAD_PORT);
    sock_address.sin_addr = *((struct in_addr*) (host->h_addr));
}

int clientConnect(sockaddr_in clin_address)
{
    int client_fd = socket(AF_INET, SOCK_STREAM,0);
    errcheck(client_fd,"Error creating socket", true);
    errcheck(connect(client_fd, (struct sockaddr *) &clin_address,sizeof(clin_address)),"Error connecting", true);

    return client_fd;
}

void sendTargets(int client_fd, const char* argv)
{
    size_t len;

    len = strlen(argv) + 1;
    errcheck(write(client_fd, &len, sizeof(len)),"Error sending data", true);
    errcheck(write(client_fd, argv, len),"Error sending data", true);
    
    errcheck(shutdown(client_fd,SHUT_WR),"Error sending eof", true);
}

void makeFile(int clin_fd, char* name, mode_t permissions, int threadNum)
{
    int file_fd;
    off_t file_size;
    string new_name = string("./threadDir") + intToString(threadNum) + "/" + string(name);
    
    //cannot preserve premissions due to root privilages
    errcheck(file_fd = open(new_name.c_str(),O_WRONLY | O_CREAT | O_TRUNC, permissions), "Error creating file",true);
    
    read(clin_fd,&file_size,sizeof(file_size));
    char buffer[BUFSIZ];
    
    //if this were a more recent linux kernal, I would be using sendfile to receive the file
    //errcheck(sendfile(file_fd,clin_fd,NULL,file_size), "Error recieving file", true);
    
    while(file_size > 0)
    {
        int bytes_rec;
        
        if(file_size < BUFSIZ)
        {bytes_rec = read(clin_fd,&buffer, file_size);}
        else
        {bytes_rec = read(clin_fd,&buffer, BUFSIZ);}
        
        errcheck(write(file_fd,&buffer,bytes_rec),"Error copying file", true);
        memset(&buffer,0,sizeof(buffer));
        file_size -= bytes_rec;
    }
    errcheck(close(file_fd),"Error closing file", true);
}

void makeDire(int i)
{
    umask(0);
    string new_name = "./threadDir" + intToString(i);
    int result = mkdir(new_name.c_str(),0777);
    //tells program to not exit if mkdir receives the "already exists" error
    if(result == -1)
    {
        if(errno != EEXIST)
        {errcheck(-1,"Error creating directory", true);}
    }
}

void receiveData(int client_fd, int threadnum)
{
    char name[256];
    size_t name_size;
    mode_t permissions;
    
    errcheck(read(client_fd,&name_size,sizeof(name_size)),"Error reading from client", true);
    errcheck(read(client_fd,&name,name_size),"Error reading from client", true);
    errcheck(read(client_fd,&permissions,sizeof(permissions)),"Error reading from client", true);
        
    makeFile(client_fd,name,permissions, threadnum);
    
}

void* threadProcess(void* argsp)
{
    threadArgs* args = (threadArgs*) argsp;
    int client_fd;
    string host(args->hostname);
    string file(args->filename);
    int thread = args-> threadNum;

    struct hostent* hostp;
    struct sockaddr_in clin_address;
    
    setupHostent(hostp, host.c_str());
    setupSockaddr(clin_address,hostp);
    
    client_fd = clientConnect(clin_address);
    sendTargets(client_fd, file.c_str());
    receiveData(client_fd, thread);
    errcheck(close(client_fd),"Error closing socket", true);
    
    pthread_exit(NULL);
}
        
int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        cout << "Error insufficient arguments\n";
        cout << "Usage:\n";
        cout << "[Hostname]" << endl;
        
        return 1;
    }
    
    
    pthread_t threads[MAX_THREADS * files_length()];
    
    Timer t;
    double usrTime;
    double sysTime;
    double WCTime;
    t.start();

    for(int i = 0; i < MAX_THREADS; ++i)
    {
        makeDire(i);
        for(int j = 0; j < files_length();++j)
        {
            threadArgs args;
            
            strcpy(args.hostname,argv[1]); 
            strcpy(args.filename,files[j]);
            args.threadNum = i;
            if(pthread_create(&threads[(files_length() * i) + j],NULL,threadProcess,(void*) &args) < 0)
            {
                perror("Error creating thread");
                exit(-1);
            }
            usleep(1000);
        }
    }
    
    for(int i = 0; i < MAX_THREADS * files_length(); ++i)
    {pthread_join(threads[i],NULL);}
    t.elapsedTime(WCTime,usrTime,sysTime);
    
    cout << "Elapsed Wallclock time: " << WCTime << endl;
    cout << "Elapsed System time:    " << WCTime << endl;
    cout << "Elapsed User time:      " << WCTime << endl;

    pthread_exit(NULL);
}
    
