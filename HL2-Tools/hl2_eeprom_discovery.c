/* Copyright (C)
*
*   2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*   hl2_eeprom_discovery.c
*
*   Build: gcc -std=c11 -Wall -O2 hl2_eeprom_discovery.c -o hl2_eeprom_discovery
*
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

static void mac(const uint8_t *m){ printf("%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]); }

int main(void){
    uint8_t q[63]={0}; q[0]=0xEF; q[1]=0xFE; q[2]=0x02;

    int fd=socket(AF_INET,SOCK_DGRAM,0); if(fd<0){perror("socket");return 1;}
    int yes=1; setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    struct timeval tv={.tv_sec=2,.tv_usec=0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));

    struct sockaddr_in any={0}; any.sin_family=AF_INET; any.sin_port=0; any.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(fd,(struct sockaddr*)&any,sizeof(any))<0){perror("bind");return 1;}

    const char* bcast="255.255.255.255";
    struct sockaddr_in dst={0}; dst.sin_family=AF_INET; dst.sin_port=htons(1024); inet_pton(AF_INET,bcast,&dst.sin_addr);
    if(sendto(fd,q,sizeof(q),0,(struct sockaddr*)&dst,sizeof(dst))!=(ssize_t)sizeof(q)){perror("sendto");}

    int found=0;
    for(;;){
        uint8_t buf[512]; struct sockaddr_in src; socklen_t sl=sizeof(src);
        ssize_t r=recvfrom(fd,buf,sizeof(buf),0,(struct sockaddr*)&src,&sl);
        if(r<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) break; perror("recvfrom"); break; }
        if(r<0x10) continue;
        if(buf[0]!=0xEF||buf[1]!=0xFE||buf[2]<0x02||buf[2]>0x03) continue; // METIS reply
        // HL2-spezifische Erweiterung:
        // [0x03..0x08] MAC, [0x09] Gateware Major, [0x0A] BoardID(0x06),
        // [0x0B] EEPROM 0x06 (Config Flags), [0x0C] EEPROM 0x07 (Reserved),
        // [0x0D..0x10] EEPROM 0x08..0x0B (Fixed IP W.X.Y.Z)
        uint8_t *macp=&buf[3];
        uint8_t gwmaj=buf[9], board=buf[10];
        uint8_t e6=buf[11], e7=buf[12], ipW=buf[13], ipX=buf[14], ipY=buf[15], ipZ=buf[16];

        char ipstr[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&src.sin_addr,ipstr,sizeof ipstr);

        printf("HL2 #%d @ %s\n",++found,ipstr);
        printf("  MAC: "); mac(macp); printf("\n");
        printf("  Gateware: %u  BoardID: 0x%02X\n",gwmaj,board);
        printf("  EEPROM[0x06] Flags: 0x%02X  (ValidIP=%u, ValidMAC=%u, DHCPfav=%u)\n",
               e6, !!(e6&0x80), !!(e6&0x40), !!(e6&0x20));
        printf("  EEPROM[0x07] Reserved: 0x%02X\n", e7);
        printf("  EEPROM Fixed IP (0x08..0x0B): %u.%u.%u.%u\n", ipW,ipX,ipY,ipZ);
    }
    if(found==0) fprintf(stderr,"Kein HL2 gefunden.\n");
    close(fd);
    return found?0:2;
}
