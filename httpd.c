#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#define POOLSIZE 10
#define BUF_LEN	8192
#define BUF_SIZE 256



//data structure
struct node{
  time_t req_time;
  time_t last_mod;
  char nfilename[50];
  struct node *next;
  int sockid;
  int filesize;
  char req_type[10];
  char addr[INET_ADDRSTRLEN];
  char protocol[25];
};
struct log_data{
  char *addr;
  time_t req;
  time_t exe;
  char req_line[50];
  char status[50];
  int size;
};
char *sbuf;
char buf[BUF_LEN];
//struct for parsed data
struct parse{
  char *command;
  char* filename;
  char *protocol;
};
typedef struct{
  struct node *curr;
  struct node *root;
} link_list;

struct q_data{
  int n_threads;
  char schd_algo[5];
  int q_time;
 };

link_list l;
struct node *ready;
int ret;
struct log_data log1;
pthread_mutex_t lock,lock1;


pthread_cond_t lock_sh;
void printUsage(void);
void *schd_method();
void logger(struct node*,time_t);
void *tp_method();
int isPresent(char *);
struct node *sjf();
struct node *pop();
char *listDir(struct node *);
char *headerFile(struct node *);
void d_logger(struct node *,time_t);
void createlist(char *,int,int,time_t,char *,char *,char *);
void insertEnd(char *,int,int,time_t,char *,char *,char *);
struct node *sort(link_list *);
void printlist(struct node *);
void parseCmd();
struct parse *parseReq(char *,char *);
void fileread(struct node *);
pthread_cond_t c_lock;
char *parseFname(),log_file[50]="\0";
struct stat st;

int i,s, sock, ch, bytes, bufsize,get=0,htmlflag=0,debug=0,logging=0;

extern char *optarg;
extern int optind;

void main(int argc, char **argv ){
  int c;
  pid_t pid,sid;
  struct q_data *queue_data;
  char *schedPolicy="FCFS",*rootdir=".";
  int n_threads=4,q_time=60;
  char *port="8080";
  struct parse *req;
  char *fname,fname1[100];
  pthread_t schd;
  struct sockaddr_in serv, remote;
  int newsock,len,flen=0;
  time_t curr_t, l_mod;
  int flag_file=0;
  char r_addr[INET_ADDRSTRLEN];
  if (argc < 2)
    {
        printUsage();
        exit(1);
    }
       while ( ( c = getopt (argc, argv, "dhl:p:r:t:n:s:") ) != -1 )
    {
        switch (c)
        {
	   
	   case 'd':
	      debug=1;
	      break;
	   case 'r':
                rootdir = optarg;
                break;
	   case 'l':
		logging=1;
		strcat(log_file,rootdir);
		if(strstr(rootdir,".")!=NULL){
		strcat(log_file,"/");
		}
		strcat(log_file,optarg);
		break;
	   case 't':
		q_time=atoi(optarg);
		break;
            case 'h':
                printUsage();
                exit(1);
		break;
            case 'p':
                port = optarg;
                if (atoi(port) < 1024)
                {
                    fprintf(stderr, "[error] Port number must be greater than or equal to 1024.\n");
                    exit(1);
                }
                break;
            case 'n':
                n_threads = atoi(optarg);
                if (n_threads < 1)
                {
                        fprintf(stderr, "[error] number of threads must be greater than 0.\n");
                        exit(1);
                }
                break;
            case 's':
                schedPolicy = optarg;
                break;
            default:
                printUsage();
                exit(1);
        }
    }
  queue_data=malloc(sizeof(struct q_data));
  strcpy(queue_data->schd_algo,schedPolicy);
  queue_data->n_threads=n_threads;
  queue_data->q_time=q_time;
  len=sizeof(remote);
  memset((void *)&serv,0,sizeof(serv));
  serv.sin_family=AF_INET;
  serv.sin_port=htons(atoi(port));
  if(debug!=1){
   if((pid=fork())<0){
     perror("fork()");
     exit(0);
  }
   if(pid>0){
    exit(EXIT_SUCCESS);
   }
   umask(0);
   sid=setsid();
   close(0);
   close(1);
   close(2);
  }
  else{
    fprintf(stderr, "myhttpd logfile: %s\n", log_file);
        fprintf(stderr, "myhttpd port number: %s\n", port);
        fprintf(stderr, "myhttpd rootdir: %s\n", rootdir);
        fprintf(stderr, "myhttpd queueing time: %d\n", q_time);
        fprintf(stderr, "myhttpd number of threads: %d\n", n_threads);
	fprintf(stderr, "myhttpd scheduling policy: %s\n", schedPolicy);
  }
    //create socket
  if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
  perror("socket()");
  exit(1);
  }
  //bind socket
  if(bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0){
    perror("bind");
    exit(1);
  }
  listen(s, 1);
  pthread_mutex_init(&lock1,NULL);
  pthread_cond_init(&c_lock,NULL);
  pthread_mutex_init(&lock, NULL);
  pthread_create(&schd,NULL,schd_method,(void *)queue_data);
  
  printf("waiting for conenctions\n");
  while(1){
    fname=NULL;
    newsock = accept(s, (struct sockaddr *) &remote, &len);
    strcpy(r_addr,inet_ntoa(remote.sin_addr));
    memset(buf,0,BUF_LEN);
    if((bytes=recv(newsock,buf, BUF_LEN, 0))<0){
      perror("recv\n");
    }
    curr_t=time(NULL);
    //parse 
      
      req=parseReq(buf,rootdir);
      
      if(strstr(req->filename,".ico")==NULL){
	
	flag_file=isPresent(req->filename);
	if(flag_file==1&&strcmp(req->command,"HEAD")!=0){
	  flen=fsize(req->filename);
	}
	memset(fname1,0,100);
	strcpy(fname1,req->filename);
	insertEnd(fname1,newsock,flen,curr_t,req->command,r_addr,req->protocol);//insert into the link list
      }
      pthread_cond_signal(&lock_sh);
    }
  pthread_join(schd,NULL);
}

//file read function.
void fileread(struct node *data)
{
    char filename[100];
    memset(filename,0,100);
    FILE *fp1;
    size_t read1; 
    char*  readBuf,buf[2000];
    fp1 = fopen( data->nfilename, "r");
    if(fp1==0){
      if(strstr(data->nfilename,"index")==NULL){
	
	readBuf=(char *) malloc(sizeof(char) * 30);
	memset(readBuf,0,sizeof(char) * 30);
	strcpy(readBuf,"404 File Not Found\n");
	if(send(data->sockid,readBuf,sizeof(char) * 30,0)<0){
	      perror("send()");
	}
      }else{
	readBuf=(char *) malloc(sizeof(char) * 2000);
	memset(readBuf,0,sizeof(char) * 2000);
 	strcpy(readBuf,listDir(data));
	if(send(data->sockid,readBuf,sizeof(char) * 2000,0)<0){
	      perror("send()");
	}
      }
    }
    else{
      readBuf = malloc( sizeof(char) * BUF_SIZE );
      while (0 < (read1 = fread( (void *)readBuf, sizeof(char),BUF_SIZE, fp1)) )
      {	  
	  if(send(data->sockid,readBuf,read1,0)<0){
	    perror("send()");
	  }
      }
      fclose(fp1);
    }
//    return readBuf;
}

//PARSING
struct parse *parseReq(char *req,char *root){
  const char *ptr = req;
  int i,flag=0;
  char c,*split;
  struct parse *pdata;
  char temp1[100],temp2[50],temp3[200],temp4[25],temp[25],temp5[50];
  pdata=malloc(sizeof(struct parse));
  memset(temp1,0,100);
  memset(temp3,0,50);
  memset(temp4,0,25);
  memset(temp5,0,50);
  memset(temp,0,25);
  pdata->command=malloc(sizeof(temp3));
  sscanf(ptr,"%s %s %s",temp3,temp1,temp4);
  strcpy(pdata->command,temp3);
  memset(temp2,0,50);
  if(strstr(temp1,"~")!=NULL){
    strcpy(temp5,temp1);
    split=strtok(temp5,"~");
    strcpy(temp1,"/home/");
    strcat(temp1,strtok(NULL,"/"));
    strcat(temp1,"/myhttpd/");
    if(strstr(temp1,".")!=NULL){
    strcat(temp1,strtok(NULL,""));
    }
    flag=1;
  }
  if(strstr(temp1,".")==NULL){
    strcat(temp1,"index.html");
  }
  if(flag==0){strcat(temp2,root);}
  strcat(temp2,temp1);
  pdata->filename=malloc(51);
  pdata->protocol=malloc(25);
  strcpy(pdata->filename,temp2);
  strcpy(pdata->protocol,temp4);
  return pdata;
}

//link list functions
struct node *pop(){
  struct node *temp;
  if(l.root!=NULL){
  temp=l.root;
  l.root=l.root->next;
  l.curr=l.root;
  temp->next=NULL;
  }
  return temp;
}
void createlist(char *data,int sockid,int size,time_t time,char *req,char *addr,char *protocol){
  pthread_mutex_lock(&lock);
  if(l.root==NULL){
    
    l.root=malloc(sizeof(struct node));
    l.root->next=NULL;
    memset(l.root->nfilename,0,50);
    strcpy(l.root->nfilename,data);
    memset(l.root->addr,0,INET_ADDRSTRLEN);
    strcpy(l.root->addr,addr);
    l.root->sockid=sockid;
    l.root->filesize=size;
    strcpy(l.root->req_type,req);
    l.root->req_time=time;
    memset(l.root->protocol,0,25);
    strcpy(l.root->protocol,protocol);
    l.curr=l.root;
  }
  pthread_mutex_unlock(&lock);
}

void insertEnd(char *data,int sockid, int size,time_t time,char *req,char *addr,char *protocol){
  pthread_mutex_lock(&lock);
  if(NULL==l.root){
    pthread_mutex_unlock(&lock);
    createlist(data,sockid,size,time,req,addr,protocol);
    
  }
  else{
    struct node *temp=malloc(sizeof(struct node));
    memset(temp->nfilename,0,50);
    strcpy(temp->nfilename,data);
    memset(temp->protocol,0,25);
    strcpy(temp->protocol,protocol);
    temp->req_time=time;
    memset(temp->req_type,0,sizeof(temp->req_type));
    strcpy(temp->req_type,req);
    memset(temp->addr,0,INET_ADDRSTRLEN);
    strcpy(temp->addr,addr);
    temp->sockid=sockid;
    temp->filesize=size;
    temp->next=NULL;
    l.curr->next=temp;
    l.curr=temp;
    pthread_mutex_unlock(&lock);
  }
}

void printlist(struct node *root){
  pthread_mutex_lock(&lock);
  while(root!=NULL){
    printf("data: %s file size:%d\n",root->nfilename,root->sockid);
    root=root->next;
  }
  pthread_mutex_unlock(&lock);
}

void *schd_method(void *arg){
  struct q_data *queue_data=(struct q_data *)arg;
  sleep(queue_data->q_time);
  pthread_t *threadpool;
  threadpool=(pthread_t *)malloc(sizeof(pthread_t)*queue_data->n_threads);
  for(i=0;i<queue_data->n_threads;i++){
  pthread_create(&threadpool[i],NULL,tp_method,NULL);
  }
  
  while(1){
    pthread_mutex_lock(&lock);
    
    pthread_mutex_lock(&lock1);
    
    if(ready==NULL){
      
      if(l.root!=NULL){
	
	if(strcmp(queue_data->schd_algo,"SJF")==0){
	
	  ready=sjf();
	}
	else {
	  ready=pop();
	}
	
      }
      else if(ready==NULL){
	pthread_mutex_unlock(&lock1);
	
	pthread_cond_wait(&lock_sh,&lock);
      }
    }
    
    pthread_mutex_unlock(&lock1);
    
    pthread_cond_signal(&c_lock);
    
    pthread_mutex_unlock(&lock);
    
  }
}
void *tp_method(){
  char *temp;
  time_t th_time;
  char header[1000];
  int flag;
  while(1){
    struct node data ;
    pthread_mutex_lock(&lock1);
    pthread_cond_wait(&c_lock,&lock1);
    if(ready!=NULL){
      data= *ready;
      ready=NULL;
      memset(header,0,1000);
      time_t th_time= time(NULL);
      flag=isPresent(data.nfilename);
	strcpy(header,(char *)headerFile(&data));
	if((send(data.sockid,header,sizeof(header),0))<0){
	  perror("send()");
	}
      if(strcmp(data.req_type,"GET")==0||strcmp(data.req_type,"get")==0){
	temp=(char *)malloc(sizeof(char)*2000);

	fileread(&data);
      }
      close(data.sockid); 
      if(logging==1){
	
	logger(&data,th_time);
	
      }
      if(debug==1){
	d_logger(&data,th_time);
      }
    }
    pthread_mutex_unlock(&lock1);
  }
}
 int fsize(char *fname){
   int size;
   char fname1[100];
   strcpy(fname1,fname);
   FILE *fp=fopen(fname,"r");
   stat(fname, &st);
   size=st.st_size;
   fclose(fp);
   return size;
}
void printUsage(void)
{
    fprintf(stderr, "Usage: myhttpd [−d] [−h] [−l file] [−p port] [−r dir] [−t time] [−n thread_num]  [−s sched]\n");

    fprintf(stderr,
            "\t−d : Enter debugging mode. That is, do not daemonize, only accept\n"
            "\tone connection at a time and enable logging to stdout.  Without\n"
            "\tthis option, the web server should run as a daemon process in the\n"
            "\tbackground.\n"
            "\t−h : Print a usage summary with all options and exit.\n"
            "\t−l file : Log all requests to the given file. See LOGGING for\n"
            "\tdetails.\n"
            "\t−p port : Listen on the given port. If not provided, myhttpd will\n"
            "\tlisten on port 8080.\n"
            "\t−r dir : Set the root directory for the http server to dir.\n"
            "\t−t time : Set the queuing time to time seconds.  The default should\n"
            "\tbe 60 seconds.\n"
            "\t−n thread_num : Set number of threads waiting ready in the execution thread pool to\n"
            "\tthreadnum.  The d efault should be 4 execution threads.\n"
            "\t−s sched : Set the scheduling policy. It can be either FCFS or SJF.\n"
            "\tThe default will be FCFS.\n"
            );
}
struct node *sjf(){
  struct node *ptr,*del;
  struct node *temp=l.root;
  int i=1,pos=0;
  if(temp!=NULL){
    ptr=l.root->next;
    while(ptr!=NULL){
      if((temp->filesize)>(ptr->filesize)){
	temp=ptr;
	pos=i;
      }
      ptr=ptr->next;
      i++;
    }
    ptr=l.root;
    for(i=0;i<=pos-2;i++){
      ptr=ptr->next;
    }
    del=ptr->next;
    if(l.curr==ptr->next){
      l.curr=ptr;
    }
   if(temp==l.root){
     l.root=l.root->next;
  }
   else if(ptr->next!=NULL){ 
    ptr->next=ptr->next->next;
   }
   else{
     l.root==NULL;
  }
    temp->next=NULL;    
  }
  return temp;
}

char *headerFile(struct node *file){
  char file_ty[20],f[30];
  char buffer[1000],*buf;
  struct stat st;
  char temp[20];
  memset(temp,0,20);
  stat(file->nfilename,&st);
  time_t m_time=st.st_mtime;
  int file_flag=isPresent(file->nfilename);
  memset(file_ty,0,20);
  memset(f,0,30);
  if(strstr(file->nfilename,"jpg")!=NULL||strstr(file->nfilename,"png")!=NULL){
   strcpy(file_ty,"image/gif");
  }
  if(strstr(file->nfilename,"html")!=NULL||strstr(file->nfilename,"txt")!=NULL){
    strcpy(file_ty,"text/html");
  }
  memset(buffer,0,1000);
  strcat(buffer,"HTTP/1.0 ");
  if(file_flag==0){
    strcpy(temp,"404 FILE NOT FOUND");
  } 
  else{
    strcpy(temp,"200 OK");
  }
  strcat(buffer,temp);
  strcat(buffer,"\n");
  strcat(buffer,"Server: httpd ver:1.0\n");
  strcat(buffer,"Last-Modified: ");
  strcat(buffer,ctime(&m_time));
  strcat(buffer,"Content-Type: ");
  strcat(buffer,file_ty);
  strcat(buffer,"\n");
  sprintf(temp,"%d bytes",file->filesize);
  strcat(buffer, "Content-Length: ");
  strcat(buffer,temp);
  strcat(buffer,"\n\n");
  buf=buffer;
  return buf;
}
char *listDir(struct node* name){
  link_list list_Dir;
  struct node *temp,*pointer;
  char buffer[2000],filename[100];
  char *ptr;
  DIR *dp;
  int i=0;
  struct stat st;
  struct dirent *ep;
  time_t time;
  char size[5],*p,temp1[50];
  int s;
  memset(buffer,0,2000);
  strcpy(filename,name->nfilename);
  p=strstr(filename,"index.html");
  while(p[i]!='\0'){
    p[i]='\0';
    i++;
  }
  dp=opendir(filename);
  if(dp==NULL){
    strcat(buffer,"404 Directory not found<br>\n");
  }
  else{
    
    while ((ep = readdir (dp))!=NULL){
      if(ep->d_name[0]!='.')
      {
	memset(temp1,0,50);
	strcpy(temp1,filename);
	strcat(temp1,ep->d_name);
	stat(temp1,&st);
	if(list_Dir.root==NULL){
	  list_Dir.root=malloc(sizeof(struct node));
	  strcpy(list_Dir.root->nfilename,ep->d_name);
	  list_Dir.root->filesize=st.st_size;
	  list_Dir.root->last_mod=st.st_mtime;
	  list_Dir.root->next=NULL;
	  list_Dir.curr=list_Dir.root;
	}
	else{
	  temp=malloc(sizeof(struct node));
	  stat(ep->d_name,&st);
	  s=st.st_size;
	  temp->filesize=s;
	  strcpy(temp->nfilename,ep->d_name);
	  temp->last_mod=st.st_mtime;
	  temp->next=NULL;
	  list_Dir.curr->next=temp;
	  list_Dir.curr=temp;
	}
      }
    }
    closedir(dp);
    pointer=list_Dir.root;
    pointer=list_Dir.root;
    while(pointer!=NULL){
      pointer=sort(&list_Dir);
      strcat(buffer,pointer->nfilename);
      strcat(buffer,"	");
      strcat(buffer,asctime(gmtime(&pointer->last_mod)));
      strcat(buffer,"	");
      sprintf(size,"%d bytes",pointer->filesize);
      strcat(buffer,size);
      strcat(buffer,"<br><br>");
      pointer=list_Dir.root;
    }
  }  
   ptr=buffer;
  return ptr;
}
int isPresent(char *filename){
  FILE *fp;
  int ret;
  fp=fopen(filename,"r");
  if(fp==NULL){
    ret=0;
  }
  else {
    ret=1;
    fclose(fp);
  }
  return ret;
}
void logger(struct node *n,time_t fin_time){
  FILE *fp;
  char buffer[500];
  int i=0,f=isPresent(n->nfilename);
  char fsize[10];
  char c;
  char status[10];
  char request_t[30],finish_t[30];
  strcpy(request_t,asctime(gmtime(&n->req_time)));
  strcpy(finish_t,asctime(gmtime(&fin_time)));
  c=request_t[0];
  while(c!='\0'){
    i++;
    c=request_t[i];
  }
  request_t[i-1]='\0';
  i=0;
  c=finish_t[0];
  while(c!='\0'){
    i++;
    c=finish_t[i];
  }
  finish_t[i-1]='\0';
  if(f==1){
    strcpy(status,"200 OK");
  }else{
    strcpy(status,"404 FILE NOT FOUND");
  }
  memset(fsize,0,10);
  sprintf(fsize,"%d",n->filesize);
  fp=fopen(log_file,"a");
  memset(buffer,0,500);
  strcat(buffer,n->addr);
  strcat(buffer," [");
  strcat(buffer,request_t);
  strcat(buffer,"] [");
  strcat(buffer,finish_t);
  strcat(buffer,"] \"");
  strcat(buffer,n->req_type);
  strcat(buffer," ");
  strcat(buffer,n->nfilename);
  strcat(buffer," ");
  strcat(buffer,n->protocol);
  strcat(buffer,"\" ");
  strcat(buffer,status);
  strcat(buffer,fsize);
  i=0;
  while(buffer[i]!='\0'){
    fputc(buffer[i],fp);
    i++;
  }
  fputc('\n',fp);
  fclose(fp);
}

void d_logger(struct node *n,time_t fin_time){
  char request_t[30],finish_t[30],status[30];
  int i=0;
  char c;
  strcpy(request_t,asctime(gmtime(&n->req_time)));
  strcpy(finish_t,asctime(gmtime(&fin_time)));
  c=request_t[0];
  while(c!='\0'){
    i++;
    c=request_t[i];
  }
  request_t[i-1]='\0';
  i=0;
  c=finish_t[0];
  while(c!='\0'){
    i++;
    c=finish_t[i];
  }
  finish_t[i-1]='\0';
  if(isPresent(n->nfilename)==1){
    strcpy(status,"200 OK");
  }else{
    strcpy(status,"404 FILE NOT FOUND");
  }
  printf("%s - [%s] [%s] \"%s %s %s\"\n",n->addr,request_t,finish_t,n->req_type,n->nfilename,n->protocol);
}
struct node *sort(link_list *list_Dir){
  struct node *ptr;
  struct node *temp=list_Dir->root;
  int i=1,pos=0;
  if(temp!=NULL){
    ptr=temp->next;
    while(ptr!=NULL){
      if(strcmp(temp->nfilename,ptr->nfilename)>0){
	temp=ptr;
	pos=i;
      }
      ptr=ptr->next;
      i++;
    }
    ptr=list_Dir->root;
    for(i=0;i<=pos-2;i++){
      ptr=ptr->next;
    }
   if(temp==list_Dir->root){
     list_Dir->root=list_Dir->root->next;
  }
  else if(ptr->next!=NULL){ 
    ptr->next=ptr->next->next;
   }
   else{
     list_Dir->root=NULL;
  }
    temp->next=NULL;    
  }
  return temp;
}