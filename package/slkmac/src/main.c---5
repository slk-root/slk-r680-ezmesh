/*
	set ipq60xx's mac address v1.0
	
	设置所有ETH网卡
	slkmac all 00:11:22:33:44:55
	
	设置单个ethx (x=0-4)
	slkmac eth0 00:11:22:33:44:55
	
	读取mac地址
	slkmac eth1 (eth0 eth1 eth2 eth3 eth4 all ath0 ath1)
	00:11:22:33:44:56
	
*/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mtd/mtd-user.h>
#include <sys/stat.h>
#include <sys/mman.h>

char mtd[20]="/dev/mmcblk0p15";

int set_mac(int sd, char *mac, int size)
{
	int fd;
	char *mmap_area = MAP_FAILED;
	struct mtd_info_user mtdInfo;
	int mtdsize = 0;

	fd = open(mtd, O_RDWR | O_SYNC);
	if (fd) {
		mtdsize = 262144;
		mmap_area = (char *) mmap(
		
			NULL, mtdsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
	} else {
		fprintf(stderr, "open %s error\n", mtd);
		return -1;
	}

	if(mmap_area != MAP_FAILED) {
		memcpy(mmap_area + sd, mac, size);
		msync(mmap_area, mtdsize, MS_SYNC);
		fsync(fd);
		munmap(mmap_area, mtdsize);
	}
	close(fd);
	return 0;
}

int get_mac(int sd,char *mac, int size)
{
	int fd;
	char *mmap_area = MAP_FAILED;
	fd = open(mtd, O_RDWR | O_SYNC);
	if (fd) {
		mmap_area = (char *) mmap(
			NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
	} else {
		fprintf(stderr, "open %s error\n", mtd);
		return -1;
	}
	if(mmap_area != MAP_FAILED) {
	memcpy(mac, mmap_area + sd, size);
	munmap(mmap_area, 0x10000);
	}
	close(fd);
	return 0;
}
//将数字转换为对应的字符串
int hex2str(unsigned int data, char* s, int len)
{
    int i;
 
    s[len] = 0;
    for (i = len - 1; i >= 0; i--, data >>= 4)
    {
        if ((data & 0xf) <= 9)
            s[i] = (data & 0xf) + '0';
        else
            s[i] = (data & 0xf) + 'A' - 0x0a;
    }
    s[0]='@';
    return 1;
}

int tolower(int c)  
{  
    if (c >= 'A' && c <= 'F')  
    {  
        return c + 'a' - 'A';  
    }  
    else  
    {  
        return c;  
    }  
}

//将十六进制的字符串转换成整数  
int htoi(char s[])  
{  
    int i = 0;  
    int n = 0;  
    if (s[0] == '0' && (s[1]=='x' || s[1]=='X'))  
    {  
        i = 2;  
    }  
    else  
    {  
        i = 0;  
    }  
    for (; (s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'F') || (s[i] >='A' && s[i] <= 'F');++i)  
    {  
        if (tolower(s[i]) > '9')  
        {  
            n = 16 * n + (10 + tolower(s[i]) - 'a');  
        }  
        else  
        {  
            n = 16 * n + (tolower(s[i]) - '0');  
        }  
    }  
    return n;  
} 

//对mac地址拆分出来的数字进行+1并判断是否溢出
int add(char *s,int data)
{
    int len;
    data++;
    //当溢出时就变为01并返回0
    if(data==256)
    {
        data=00;
        len=sizeof(data)-1;
        hex2str(data,s,len);
        return 0;
    }
        len=sizeof(data)-1;
        hex2str(data,s,len);
        return 1;
}

char  macstr_to_hex(const unsigned char *str)
{
	unsigned char c;
	const unsigned char *p;
	if ( NULL == str)
	{
		goto err;
	}

	p = str;

		if (*p == '-' || *p == ':')
		{
			p++;
		}
		
		if (*p >= '0' && *p <= '9')
		{
			c  = *p++ - '0';
		}
		else if (*p >= 'a' && *p <= 'f')
		{
			c  = *p++ - 'a' + 0xa;
		}
		else if (*p >= 'A' && *p <= 'F')
		{
			c  = *p++ - 'A' + 0xa;
		}
		else
		{
			goto err;
		}

		c <<= 4;	/* high 4bits of a byte */

		if (*p >= '0' && *p <= '9')
		{
			c |= *p++ - '0';
		}
		else if (*p >= 'a' && *p <= 'f')
		{
			c |= (*p++ - 'a' + 0xa); 
		}
		else if (*p >= 'A' && *p <= 'F')
		{
			c |= (*p++ - 'A' + 0xa);
		}
		else
		{
			goto err;
		}
		

	return c;

err:
	return -1;
}
//对mac地址运算操作
int mac_add(unsigned char *hmac,char *str1,int to)
{
        unsigned char str[20];
		unsigned char mac[20];
		unsigned char p1[6][3];
		memset(mac, 0, 6);
		memset(str,0,20);
		memset(p1,0,6);
        int i=0;
		int t=0;
        char *s = malloc(sizeof(char));
        strcpy(str,str1);
		
        //以：做为分割符对mac地址进行分割；
        char *p = strtok(str, ":");  // 首次调用时，s 指向需要分割的字符串
        while(p != NULL)
        {
            strcpy(p1[i],p);
            p= strtok(NULL, ":"); // 此后每次调用，均使用 NULL 代替。
            i++;
        }
		
		if(to == 0)
			goto err; //如果是第一个就无需进行+1，直接输出

        //mac地址进行+1;
        for (int i = 5,j=0; i>=0; i--,j++)
        {
            int data=htoi(p1[i]);
             //当溢出时或者第一次进行mac地址+1时t==0；
           if(t==0)
           {
             t=add(s,data);
             char *p=strtok(s,"@");
             strcpy(p1[i],p);
           }
        }
        
        //对字符串数组进行拼接成mac格式
        for(int j=0;j<=5;j++)
        {
			strcat(mac,p1[j]);
			if(j!=5)
			strcat(mac,":");
		    hmac[j]=macstr_to_hex(p1[j]);

        }
    strcpy(str1,mac);
    free(s);
    return 0;
	
	err:
		for(int j=0;j<=5;j++)
		    hmac[j]=macstr_to_hex(p1[j]);
		return -1;
		 
}

//字符传化为字符串；
void hex_to_str(char *hex,  int hex_len, char *str)
{
    int i, pos=0;

    for(i=0; i<=hex_len; i++)
    {
		sprintf(str+pos, "%c", ':');
		pos +=1;
		
        sprintf(str+pos, "%02X", hex[i]);
        pos +=2;
		
    }

}
//对mac进行只读的加法；
int read_mac(char *mac,int max)
{
	char str_data[128];
	hex_to_str(mac, 6, str_data);
	for(int i=0;i<=max;i++)
	{
		//memset(mac, 0, 6);
		mac_add(mac,str_data,i);
	} 
}

int main(int argc, char* argv[])
{
	unsigned char mac[6];
	unsigned char hmac[6];
	memset(mac, 0, 6);
	memset(hmac, 0, 6);
	if (argc == 3) {
		if(strcmp(argv[1],"all")==0)
		{
			char str[20];
			strcpy(str,argv[2]);
			for(int i=0;i<=4;i++)
			{
				memset(hmac, 0, 6); 
				mac_add(hmac,str,i);
				set_mac(i*6,hmac, 6);
				get_mac(i*6,mac, 6);
				printf("eth%d:%02X:%02X:%02X:%02X:%02X:%02X\n",i,mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			}
		}
		else{
			printf("error\r\n");
		}		
		
	}else if (argc == 2){ 
		if(strcmp(argv[1],"eth0")==0){
			get_mac(0,mac, 6);
			printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		else if(strcmp(argv[1],"eth1")==0){
			get_mac(6,mac, 6);
			printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		else if(strcmp(argv[1],"eth2")==0){
			get_mac(12,mac, 6);
			printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		else if(strcmp(argv[1],"eth3")==0){
			get_mac(18,mac, 6);
			printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		else if(strcmp(argv[1],"eth4")==0){
			get_mac(24,mac, 6);
			printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		else if(strcmp(argv[1],"all")==0)
		{
			for(int i=0;i<=4;i++)
			{
				get_mac(i*6,mac, 6);
				printf("eth%d:%02X:%02X:%02X:%02X:%02X:%02X\n",i,mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
			}
		}
		else if(strcmp(argv[1],"ath0")==0)
		{
				get_mac(0,mac, 6);
				//printf("ath0:%02X:%02X:%02X:%02X:%02X:%02X\n",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
                read_mac(mac,5);   //对其进行只读的加法；
			    printf("%02X:%02X:%02X:%02X:%02X:%02X",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
		}
		else if(strcmp(argv[1],"ath1")==0)
		{
				get_mac(0,mac, 6);
				//printf("ath0:%02X:%02X:%02X:%02X:%02X:%02X\n",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
                read_mac(mac,6);   //对其进行只读的加法；
			    printf("%02X:%02X:%02X:%02X:%02X:%02X",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
		}
		else{
			printf("error\r\n");
		}
	}else if(argc == 1){
			printf("eth0:%02X:%02X:%02X:%02X:%02X:%02X\n",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); //不输入任何参数就输出全部；
	}

	return 0;
}