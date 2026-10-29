// SPDX-License-Identifier: GPL-2.0-or-later
// Disposable, explicitly authorized fixture setup only. No root user command.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <ifaddrs.h>
#include <limits.h>
#include <linux/capability.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/nsfs.h>
#include <linux/rtnetlink.h>
#include <linux/securebits.h>
#include <net/if.h>
#include <net/route.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

enum { PAYLOAD_MAX=131072, IMAGE_MAX=2097152, COUNT_MAX=256, STRING_MAX=16384 };
static pid_t owned_child=-1;
static int owned_pidfd=-1;
static volatile sig_atomic_t cancelled=0;
static const char* phase="authorization";
static void on_signal(int sig){(void)sig;cancelled=1;}
static void reap_owned(void){if(owned_child>0){if(owned_pidfd>=0){(void)syscall(SYS_pidfd_send_signal,owned_pidfd,SIGKILL,NULL,0);}else{(void)kill(owned_child,SIGKILL);}while(waitpid(owned_child,NULL,0)<0&&errno==EINTR){}owned_child=-1;}}
static void fail(const char* why){int e=errno;reap_owned();fprintf(stderr,"FAIL public namespace admin: %s (errno=%d)\n",why,e);exit(1);}
static void need(int condition,const char* why){if(!condition){fail(why);}}
static uint64_t number(const char* s,uint64_t maximum){char* end=NULL;errno=0;need(s&&*s&&strspn(s,"0123456789")==strlen(s),"numeric input");unsigned long long n=strtoull(s,&end,10);need(!errno&&end&&!*end&&n<=maximum,"numeric bound");return (uint64_t)n;}
static uint64_t mono(void){struct timespec t;need(clock_gettime(CLOCK_MONOTONIC,&t)==0,"monotone clock");return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static size_t read_bounded(int fd,void* data,size_t maximum){size_t done=0;for(;;){need(done<maximum,"read size bound");ssize_t n=read(fd,(char*)data+done,maximum-done);if(n<0&&errno==EINTR){continue;}need(n>=0,"read");if(!n){return done;}done+=(size_t)n;}}
static void write_exact(int fd,const void* data,size_t size){while(size){ssize_t n=write(fd,data,size);if(n<0&&errno==EINTR){continue;}need(n>0,"write");data=(const char*)data+n;size-=(size_t)n;}}
static int same(int a,int b){struct stat x,y;need(fstat(a,&x)==0&&fstat(b,&y)==0,"namespace fstat");return x.st_dev==y.st_dev&&x.st_ino==y.st_ino;}
// Failure diagnostics only: fixed kernel metadata, never payload/environment.
static void proc_diagnostic(pid_t pid,const char* leaf,int saved_errno){
 fprintf(stderr,"PROC_OPEN phase=%s pid=%ld leaf=%s ruid=%u euid=%u rgid=%u egid=%u dumpable=%d errno=%d\n",phase,(long)pid,leaf,getuid(),geteuid(),getgid(),getegid(),prctl(PR_GET_DUMPABLE,0,0,0,0),saved_errno);
 const char* paths[]={"/proc/self/attr/current","/proc/self/status"};
 for(unsigned i=0;i<2;++i){
  int fd=open(paths[i],O_RDONLY|O_CLOEXEC);if(fd<0){continue;}
  char text[8192];ssize_t n=read(fd,text,sizeof(text)-1);close(fd);if(n<=0){continue;}text[n]=0;
  if(i==0){for(ssize_t j=0;j<n;++j){if((unsigned char)text[j]<32||(unsigned char)text[j]>126){text[j]=' ';}}fprintf(stderr,"PROC_OPEN label=%.*s\n",(int)n,text);}
  else{const char* keys[]={"CapInh:","CapPrm:","CapEff:","CapBnd:","CapAmb:","NoNewPrivs:"};for(unsigned j=0;j<6;++j){char* value=strstr(text,keys[j]);if(value!=NULL){size_t width=strcspn(value,"\n");if(width<=80){fprintf(stderr,"PROC_OPEN %.*s\n",(int)width,value);}}}}
 }
 errno=saved_errno;
}
static int proc_open(pid_t pid,const char* leaf,int flags){char path[128];need(snprintf(path,sizeof(path),"/proc/%ld/%s",(long)pid,leaf)>0,"proc path");int fd=open(path,flags|O_CLOEXEC);if(fd<0){int saved_errno=errno;proc_diagnostic(pid,leaf,saved_errno);errno=saved_errno;fail("owned proc open");}return fd;}
static void token(int fd,char expected,uint64_t deadline,int watcher){for(;;){need(!cancelled&&mono()<deadline,"setup cancelled/expired");struct pollfd p[2]={{fd,POLLIN,0},{watcher,POLLIN,0}};int n=poll(p,watcher>=0?2:1,100);if(n<0&&errno==EINTR){continue;}need(n>=0,"setup poll");need(watcher<0||!p[1].revents,"invoker exited during setup");if(p[0].revents){char c=0;need(read(fd,&c,1)==1&&c==expected,"setup token/EOF");return;}}}
static unsigned cap_count(void){unsigned cap=0;for(;cap<1024;++cap){errno=0;long n=prctl(PR_CAPBSET_READ,cap,0,0,0);if(n<0&&errno==EINVAL){break;}need(n==0||n==1,"runtime capability enumeration");}need(cap>0&&cap<1024,"runtime capability bound");return cap;}
static void verify_dropped(uid_t uid,gid_t gid,unsigned caps){uid_t r,e,s;gid_t gr,ge,gs;need(getresuid(&r,&e,&s)==0&&r==uid&&e==uid&&s==uid,"caller UID drop");need(getresgid(&gr,&ge,&gs)==0&&gr==gid&&ge==gid&&gs==gid&&getgroups(0,NULL)==0,"caller GID/groups drop");struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};struct __user_cap_data_struct d[2]={{0}};need(syscall(SYS_capget,&h,d)==0,"capget");for(unsigned i=0;i<2;++i){need(!(d[i].effective|d[i].permitted|d[i].inheritable),"nonzero capability set");}for(unsigned i=0;i<caps;++i){need(prctl(PR_CAPBSET_READ,i,0,0,0)==0&&prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_IS_SET,i,0,0)==0,"bounding/ambient capability drop");}need(prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0)==1,"NoNewPrivs");}
static unsigned drop(uid_t uid,gid_t gid,int groups_locked){if(groups_locked){need(getgroups(0,NULL)==0,"locked supplementary groups were not empty");}else{need(setgroups(0,NULL)==0,"clear supplementary groups");}unsigned caps=cap_count();need(prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_CLEAR_ALL,0,0,0)==0,"ambient clear");for(unsigned i=0;i<caps;++i){need(prctl(PR_CAPBSET_DROP,i,0,0,0)==0,"bounding capability clear");}need(prctl(PR_SET_SECUREBITS,SECBIT_NOROOT|SECBIT_NOROOT_LOCKED,0,0,0)==0&&prctl(PR_SET_KEEPCAPS,0,0,0,0)==0,"root semantics lock");need(setresgid(gid,gid,gid)==0&&setresuid(uid,uid,uid)==0,"set caller IDs");struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};struct __user_cap_data_struct d[2]={{0}};need(syscall(SYS_capset,&h,d)==0&&prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0,"capability/NNP seal");verify_dropped(uid,gid,caps);return caps;}
static void caller(pid_t pid,uint64_t start,uid_t uid,gid_t* gid){char text[16384];int fd=proc_open(pid,"status",O_RDONLY);size_t n=read_bounded(fd,text,sizeof(text)-1);close(fd);text[n]=0;unsigned ur,ue,us,uf,gr,ge,gs,gf;char* u=strstr(text,"\nUid:");char* g=strstr(text,"\nGid:");need(u&&g&&sscanf(u,"\nUid:\t%u\t%u\t%u\t%u",&ur,&ue,&us,&uf)==4&&sscanf(g,"\nGid:\t%u\t%u\t%u\t%u",&gr,&ge,&gs,&gf)==4,"kernel invoker IDs");need(ur==uid&&ue==uid&&us==uid&&uf==uid&&gr==ge&&gr==gs&&gr==gf&&gr!=UINT_MAX,"invoker ownership");*gid=(gid_t)gr;fd=proc_open(pid,"stat",O_RDONLY);n=read_bounded(fd,text,sizeof(text)-1);close(fd);text[n]=0;char* end=strrchr(text,')');need(end&&end[1]==' ',"kernel invoker stat");char* save=NULL;char* value=strtok_r(end+2," ",&save);for(unsigned field=3;field<22&&value;++field){value=strtok_r(NULL," ",&save);}need(value&&number(value,UINT64_MAX)==start,"invoker start time");}
static void check_sealed(int fd,uid_t uid,gid_t gid,size_t maximum){struct stat st;need(fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==uid&&st.st_gid==gid&&st.st_size>0&&(uint64_t)st.st_size<maximum,"sealed object ownership/bound");int required=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;int seals=fcntl(fd,F_GET_SEALS);need(seals>=0&&(seals&required)==required,"sealed object immutable");}
static int sealed(pid_t pid,int number_fd,uid_t uid,gid_t gid,size_t maximum){char leaf[64];need(snprintf(leaf,sizeof(leaf),"fd/%d",number_fd)>0,"sealed fd path");int fd=proc_open(pid,leaf,O_RDONLY);check_sealed(fd,uid,gid,maximum);return fd;}
static void map_file(int directory,const char* leaf,const char* value){int fd=openat(directory,leaf,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);need(fd>=0,"owned child map open");write_exact(fd,value,strlen(value));need(close(fd)==0,"owned child map close");}
static void maps(pid_t child,uid_t uid,gid_t gid){int directory=proc_open(child,"",O_RDONLY|O_DIRECTORY);char value[128];map_file(directory,"setgroups","deny");int n=snprintf(value,sizeof(value),"0 0 65536\n");if(uid>=65536){n+=snprintf(value+n,sizeof(value)-(size_t)n,"%u %u 1\n",uid,uid);}need(n>0&&(size_t)n<sizeof(value),"UID map bound");map_file(directory,"uid_map",value);n=snprintf(value,sizeof(value),"0 0 65536\n");if(gid>=65536){n+=snprintf(value+n,sizeof(value)-(size_t)n,"%u %u 1\n",gid,gid);}need(n>0&&(size_t)n<sizeof(value),"GID map bound");map_file(directory,"gid_map",value);close(directory);}
static void namespaces(int parent_net,int parent_user){int net=open("/proc/self/ns/net",O_RDONLY|O_CLOEXEC),user=open("/proc/self/ns/user",O_RDONLY|O_CLOEXEC);need(net>=0&&user>=0&&!same(net,parent_net)&&!same(user,parent_user),"actual namespace separation");need(ioctl(parent_net,NS_GET_NSTYPE)==CLONE_NEWNET&&ioctl(parent_user,NS_GET_NSTYPE)==CLONE_NEWUSER,"actual parent namespace descriptor types");int owner=ioctl(net,NS_GET_USERNS);need(owner>=0&&same(owner,user),"network owning user namespace");close(owner);close(user);close(net);struct if_nameindex* links=if_nameindex();need(links!=NULL,"interface inventory");unsigned count=0;for(struct if_nameindex* it=links;it->if_index;++it){need(!strcmp(it->if_name,"lo"),"external interface refused");++count;}if_freenameindex(links);need(count==1,"only loopback required");}
// A fresh Linux namespace may expose unreachable lo ::/0 sentinels.
// Only a REJECT route with UP clear is unusable; every usable default fails.
static void no_default_routes(void){
 const char* files[]={"/proc/net/route","/proc/net/ipv6_route"};
 for(unsigned i=0;i<2;++i){
  FILE* f=fopen(files[i],"re");need(f!=NULL,"route inventory");char line[1024];unsigned lines=0;
  while(fgets(line,sizeof(line),f)){
   need(++lines<=64&&strchr(line,'\n')!=NULL,"route inventory bound");
   char iface[32];unsigned flags=0;int is_default=0;
   if(i==0){
    char dest[32],gateway[32];if(!strncmp(line,"Iface",5)){continue;}
    need(sscanf(line,"%31s %31s %31s %x",iface,dest,gateway,&flags)==4,"IPv4 route shape");
    is_default=!strcmp(dest,"00000000");
   }else{
    char dest[33],prefix[3];
    need(sscanf(line,"%32s %2s %*32s %*2s %*32s %*8s %*8s %*8s %x %31s",dest,prefix,&flags,iface)==4,"IPv6 route shape");
    is_default=!strcmp(dest,"00000000000000000000000000000000")&&!strcmp(prefix,"00");
   }
   need(!strcmp(iface,"lo"),"external route refused");
   need(!is_default||(!(flags&RTF_UP)&&(flags&RTF_REJECT)),"usable default route refused");
  }
  need(!ferror(f),"route read");fclose(f);
 }
}
static void address_inventory(unsigned expected){
 const char* docs[]={"203.0.113.1","203.0.113.2","203.0.113.3","2001:db8::1","2001:db8::2","2001:db8::3"};
 struct ifaddrs* list=NULL;need(getifaddrs(&list)==0,"address inventory");unsigned observed=0,loopback=0;
 for(struct ifaddrs* it=list;it!=NULL;it=it->ifa_next){
  need(!strcmp(it->ifa_name,"lo"),"external address interface");
  if(it->ifa_addr==NULL){continue;}
  int family=it->ifa_addr->sa_family;
  if(family!=AF_INET&&family!=AF_INET6){continue;}
  const void* bytes=family==AF_INET?(const void*)&((struct sockaddr_in*)it->ifa_addr)->sin_addr:(const void*)&((struct sockaddr_in6*)it->ifa_addr)->sin6_addr;
  unsigned char known[16];size_t width=family==AF_INET?4:16;
  need(inet_pton(family,family==AF_INET?"127.0.0.1":"::1",known)==1,"fixed loopback address");
  if(!memcmp(bytes,known,width)){unsigned bit=family==AF_INET?1u:2u;need(!(loopback&bit),"duplicate loopback address");loopback|=bit;continue;}
  unsigned match=0;
  for(unsigned i=0;i<6;++i){if((i<3?AF_INET:AF_INET6)==family){need(inet_pton(family,docs[i],known)==1,"fixed documentation address");if(!memcmp(bytes,known,width)){match=1u<<i;break;}}}
  need(match!=0&&!(observed&match),"unexpected/duplicate private address");observed|=match;
 }
 freeifaddrs(list);need(observed==expected,"exact six-address setup inventory");
}
static void net_ack(int fd,struct nlmsghdr* request){static uint32_t sequence=1;request->nlmsg_seq=sequence++;request->nlmsg_flags|=NLM_F_REQUEST|NLM_F_ACK;struct sockaddr_nl kernel={.nl_family=AF_NETLINK};need(sendto(fd,request,request->nlmsg_len,0,(struct sockaddr*)&kernel,sizeof(kernel))==(ssize_t)request->nlmsg_len,"fixed netlink request");_Alignas(struct nlmsghdr) char buffer[4096];struct sockaddr_nl from;memset(&from,0,sizeof(from));socklen_t size=sizeof(from);ssize_t n=recvfrom(fd,buffer,sizeof(buffer),0,(struct sockaddr*)&from,&size);need(n>0&&size==sizeof(from)&&!from.nl_pid,"kernel netlink acknowledgement");int remaining=(int)n;for(struct nlmsghdr* h=(struct nlmsghdr*)buffer;NLMSG_OK(h,remaining);h=NLMSG_NEXT(h,remaining)){if(h->nlmsg_seq==request->nlmsg_seq){need(h->nlmsg_type==NLMSG_ERROR&&h->nlmsg_len>=NLMSG_LENGTH(sizeof(struct nlmsgerr)),"netlink ACK shape");struct nlmsgerr* error=NLMSG_DATA(h);if(error->error){errno=-error->error;fail("fixed namespace network setup");}return;}}fail("missing correlated netlink ACK");}
static void address(int fd,int family,const char* value,unsigned index){struct {struct nlmsghdr h;struct ifaddrmsg a;char data[64];} message;memset(&message,0,sizeof(message));message.h.nlmsg_len=NLMSG_LENGTH(sizeof(message.a));message.h.nlmsg_type=RTM_NEWADDR;message.h.nlmsg_flags=NLM_F_CREATE|NLM_F_EXCL;message.a.ifa_family=(unsigned char)family;message.a.ifa_prefixlen=family==AF_INET?32:128;message.a.ifa_scope=RT_SCOPE_UNIVERSE;message.a.ifa_index=index;if(family==AF_INET6){message.a.ifa_flags=IFA_F_NODAD;}unsigned char bytes[16];size_t width=family==AF_INET?4:16;need(inet_pton(family,value,bytes)==1,"fixed documentation address");for(unsigned type=IFA_ADDRESS;type<=IFA_LOCAL;++type){if(family==AF_INET6&&type==IFA_LOCAL){continue;}struct rtattr* a=(struct rtattr*)((char*)&message+NLMSG_ALIGN(message.h.nlmsg_len));a->rta_type=(unsigned short)type;a->rta_len=(unsigned short)RTA_LENGTH(width);memcpy(RTA_DATA(a),bytes,width);message.h.nlmsg_len=(uint32_t)(NLMSG_ALIGN(message.h.nlmsg_len)+RTA_ALIGN(a->rta_len));}net_ack(fd,&message.h);}
static void setup_network(int net,int user){namespaces(net,user);no_default_routes();address_inventory(0);unsigned index=if_nametoindex("lo");need(index,"loopback index");int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_ROUTE);need(fd>=0,"owned route socket");struct timeval limit={.tv_sec=1};need(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit))==0,"netlink budget");struct {struct nlmsghdr h;struct ifinfomsg i;} link;memset(&link,0,sizeof(link));link.h.nlmsg_len=NLMSG_LENGTH(sizeof(link.i));link.h.nlmsg_type=RTM_NEWLINK;link.i.ifi_family=AF_UNSPEC;link.i.ifi_index=(int)index;link.i.ifi_change=IFF_UP;link.i.ifi_flags=IFF_UP;net_ack(fd,&link.h);const char* v4[]={"203.0.113.1","203.0.113.2","203.0.113.3"};const char* v6[]={"2001:db8::1","2001:db8::2","2001:db8::3"};for(unsigned i=0;i<3;++i){namespaces(net,user);no_default_routes();address(fd,AF_INET,v4[i],index);namespaces(net,user);no_default_routes();address(fd,AF_INET6,v6[i],index);}close(fd);namespaces(net,user);no_default_routes();address_inventory(63);}
static uint32_t take32(const unsigned char** cursor,const unsigned char* end){need((size_t)(end-*cursor)>=4,"payload integer bound");uint32_t n;memcpy(&n,*cursor,4);*cursor+=4;return ntohl(n);}
static char* string(const unsigned char** cursor,const unsigned char* end){uint32_t n=take32(cursor,end);need(n&&n<=STRING_MAX&&(size_t)(end-*cursor)>=n&&!memchr(*cursor,0,n),"payload string bound");char* s=malloc((size_t)n+1);need(s!=NULL,"payload allocation");memcpy(s,*cursor,n);s[n]=0;*cursor+=n;return s;}
static void execute_user(uid_t uid,gid_t gid,int net,int user,unsigned caps){verify_dropped(uid,gid,caps);int input=STDIN_FILENO;check_sealed(input,uid,gid,PAYLOAD_MAX);need(lseek(input,0,SEEK_CUR)==0,"sealed stdin payload offset");unsigned char* data=malloc(PAYLOAD_MAX);need(data!=NULL,"payload allocation");size_t size=read_bounded(input,data,PAYLOAD_MAX);int null_input=open("/dev/null",O_RDONLY|O_CLOEXEC);need(null_input>=0&&dup2(null_input,STDIN_FILENO)==STDIN_FILENO,"restore ordinary empty subject stdin");close(null_input);need(size>=16&&!memcmp(data,"BBPUB1\0\0",8),"payload header");const unsigned char* at=data+8;const unsigned char* end=data+size;uint32_t argc=take32(&at,end),envc=take32(&at,end);need(argc&&argc<=COUNT_MAX&&envc<=COUNT_MAX,"payload count bounds");char** argv=calloc((size_t)argc+1,sizeof(char*));need(argv!=NULL,"argument allocation");for(uint32_t i=0;i<argc;++i){argv[i]=string(&at,end);}need(argv[0][0]=='/',"absolute user ELF");need(clearenv()==0,"clear administrative environment");for(uint32_t i=0;i<envc;++i){char* value=string(&at,end);char* equal=strchr(value,'=');need(equal&&equal!=value,"environment shape");*equal=0;need(setenv(value,equal+1,1)==0,"user environment");free(value);}need(at==end,"payload trailing data");free(data);char value[32];snprintf(value,sizeof(value),"%d",net);need(setenv("BIGLYBTC_OWNED_PUBLIC_NET_FD",value,1)==0,"net descriptor environment");snprintf(value,sizeof(value),"%d",user);need(setenv("BIGLYBTC_OWNED_PUBLIC_USER_FD",value,1)==0,"user descriptor environment");verify_dropped(uid,gid,caps);namespaces(net,user);no_default_routes();address_inventory(63);int driver=open(argv[0],O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;need(driver>=0&&fstat(driver,&st)==0&&S_ISREG(st.st_mode)&&!(st.st_mode&(S_ISUID|S_ISGID)),"ordinary user ELF");unsigned char magic[4];need(pread(driver,magic,4,0)==4&&!memcmp(magic,"\177ELF",4),"only actual ELF execution");errno=0;ssize_t capability=fgetxattr(driver,"security.capability",NULL,0);need(capability<0&&(errno==ENODATA||errno==ENOTSUP),"file capabilities refused");need(fcntl(net,F_SETFD,0)==0&&fcntl(user,F_SETFD,0)==0,"namespace proof inheritance");DIR* directory=opendir("/proc/self/fd");need(directory!=NULL,"descriptor inventory");struct dirent* entry;while((entry=readdir(directory))){char* tail=NULL;long n=strtol(entry->d_name,&tail,10);if(tail&&!*tail&&n>=3&&n!=net&&n!=user&&n!=driver&&n!=dirfd(directory)){close((int)n);}}closedir(directory);printf("BBPUB_DROP uid=%u gid=%u groups=0 capabilities=0 bounding=0 ambient=0 nnp=1\n",uid,gid);fflush(stdout);fexecve(driver,argv,environ);fail("user ELF exec");}
int main(int argc,char** argv){need(argc==7,"fixed admin arguments");const char* mode=argv[1];const char* authority=!strcmp(mode,"pkexec")?getenv("PKEXEC_UID"):!strcmp(mode,"sudo")?getenv("SUDO_UID"):NULL;need(authority!=NULL,"explicit authorized mode/caller");uid_t uid=(uid_t)number(authority,UINT_MAX-1);need(uid!=0,"nonroot caller required");const char* other=getenv(!strcmp(mode,"pkexec")?"SUDO_UID":"PKEXEC_UID");need(!other||number(other,UINT_MAX-1)==uid,"conflicting caller identity");pid_t invoker=(pid_t)number(argv[2],INT_MAX);int image_fd=(int)number(argv[3],INT_MAX),payload_fd=(int)number(argv[4],INT_MAX);uint64_t start=number(argv[5],UINT64_MAX);need(!strcmp(argv[6],"fixed-public-fixture-v1")&&invoker>1&&image_fd>=3&&payload_fd>=3&&image_fd!=payload_fd,"fixed invocation");uid_t r,e,s;gid_t gr,ge,gs;need(getresuid(&r,&e,&s)==0&&r==0&&e==0&&s==0&&getresgid(&gr,&ge,&gs)==0,"actual root authorization");gid_t gid;phase="root-caller-proof";caller(invoker,start,uid,&gid);if(!strcmp(mode,"sudo")){need(getenv("SUDO_GID")&&number(getenv("SUDO_GID"),UINT_MAX-1)==gid,"sudo/kernel caller group");}need(clearenv()==0,"root environment scrub");struct rlimit core={0,0};need(setrlimit(RLIMIT_CORE,&core)==0,"no privileged core");phase="root-sealed-image-proof";int image=sealed(invoker,image_fd,uid,gid,IMAGE_MAX),self=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);need(self>=0&&same(image,self),"actual sealed approved image");close(image);close(self);int watcher=(int)syscall(SYS_pidfd_open,invoker,0);need(watcher>=0,"invoker pidfd");phase="root-invoker-continuity";caller(invoker,start,uid,&gid);int net=open("/proc/self/ns/net",O_RDONLY|O_CLOEXEC),user=open("/proc/self/ns/user",O_RDONLY|O_CLOEXEC);need(net>=0&&user>=0,"actual parent namespace descriptors");int oldnet=proc_open(invoker,"ns/net",O_RDONLY),olduser=proc_open(invoker,"ns/user",O_RDONLY);need(same(net,oldnet)&&same(user,olduser),"invoker/admin namespace continuity");close(oldnet);close(olduser);int ready[2],go[2];need(pipe2(ready,O_CLOEXEC)==0&&pipe2(go,O_CLOEXEC)==0,"owned setup pipes");struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=on_signal;sigemptyset(&action.sa_mask);need(sigaction(SIGTERM,&action,NULL)==0&&sigaction(SIGINT,&action,NULL)==0&&sigaction(SIGHUP,&action,NULL)==0,"owned cancellation handlers");signal(SIGPIPE,SIG_IGN);struct sigaction child_action;memset(&child_action,0,sizeof(child_action));child_action.sa_handler=SIG_DFL;sigemptyset(&child_action.sa_mask);need(sigaction(SIGCHLD,&child_action,NULL)==0,"owned child must remain waitable");uint64_t setup_end=mono()+5000;pid_t parent=getpid();pid_t child=fork();need(child>=0,"owned child fork");if(child==0){close(ready[0]);close(go[1]);need(prctl(PR_SET_PDEATHSIG,SIGKILL,0,0,0)==0&&getppid()==parent,"setup parent lifetime");need(setgroups(0,NULL)==0,"clear groups BEFORE userns deny");need(unshare(CLONE_NEWUSER|CLONE_NEWNET)==0,"fresh fixed user/network namespace");write_exact(ready[1],"R",1);token(go[0],'M',setup_end,watcher);close(go[0]);phase="child-network-setup";setup_network(net,user);phase="child-credential-drop";unsigned caps=drop(uid,gid,1);need(prctl(PR_SET_PDEATHSIG,SIGKILL,0,0,0)==0&&getppid()==parent,"dropped parent lifetime");namespaces(net,user);write_exact(ready[1],"D",1);close(ready[1]);close(watcher);phase="dropped-user-payload";execute_user(uid,gid,net,user,caps);return 1;}owned_child=child;owned_pidfd=(int)syscall(SYS_pidfd_open,child,0);need(owned_pidfd>=0,"owned child pidfd");close(ready[1]);close(go[0]);close(net);close(user);token(ready[0],'R',setup_end,watcher);phase="root-owned-child-maps";maps(child,uid,gid);write_exact(go[1],"M",1);close(go[1]);token(ready[0],'D',setup_end,watcher);close(ready[0]);(void)drop(uid,gid,0);uint64_t end=mono()+240000;for(;;){int status=0;pid_t done=waitpid(child,&status,WNOHANG);if(done==child){owned_child=-1;close(owned_pidfd);close(watcher);return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}need(done==0||(done<0&&errno==EINTR),"owned child wait");struct pollfd p[2]={{owned_pidfd,POLLIN,0},{watcher,POLLIN,0}};int n=poll(p,2,100);if(n<0&&errno==EINTR){continue;}need(n>=0&&!cancelled&&mono()<end&&!p[1].revents,"owned child/invoker lifetime");}}
