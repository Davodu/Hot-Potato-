#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "potato.h"
#define WRITE_INTERVAL 500*1000
#define MAX_BUF 1024

int play_game(int player_num)
{
    // Open pipe from master
    // write to master our number
    // get from master total,
    // add new fifo fds to write/read
    // wait for potato or shutdown
    int master_in_fifo, master_out_fifo;
    char master_in_fifo_str[50], master_out_fifo_str[50];
    int left_in_fifo = -1, left_out_fifo;
    char left_in_fifo_str[50], left_out_fifo_str[50];
    int right_in_fifo = -1, right_out_fifo;
    char right_in_fifo_str[50], right_out_fifo_str[50];
    char msg[50];
    fd_set active_fd_set, read_fd_set;
    int read_fd;
    char buf[2* sizeof(POTATO_T)];
    int num_read = 0;
    int code;
    POTATO_T * potato_ptr = NULL;
    int next_player, num_players = 0;

    // seed rand number generator;
    srand( (unsigned int) time(NULL) );
    //create fifo names - variable param "player_num"
    snprintf(master_out_fifo_str, sizeof(master_out_fifo_str), "/tmp/p%d_master", player_num);
    snprintf(master_in_fifo_str, sizeof(master_in_fifo_str), "/tmp/master_p%d", player_num);
    
    //open read and write fifos
    master_out_fifo = open(master_out_fifo_str,  O_WRONLY);
    if (0 > master_out_fifo) {
        printf("Failed to open %s fifo for writing \n", master_out_fifo_str);
        return (-1);
    }

    master_in_fifo = open(master_in_fifo_str,  O_RDONLY);
    if (0 > master_in_fifo) {
        printf("Failed to open %s fifo for reading \n", master_in_fifo_str);
        return (-1);
    }
    //initilize fd set to be the empty set 
    FD_ZERO(&active_fd_set);
    //add file descriptor to the desciptor set 
    FD_SET(master_in_fifo, &active_fd_set);
    
    // store player number in string "msg" to be passed to the ringmaster(master_out_fifo will not be enqueued)
    snprintf(msg, sizeof(msg), "%d", player_num);
    write(master_out_fifo, msg, strlen(msg) +1); // +1 for the null char
    
    //sleep to allow last player process the message if its the last player to connect, sleep
    

    while(1) {
      
        read_fd_set = active_fd_set;
        /* Block until input arrives on one or more active fifos. */
        if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            printf("Failed to select\n");
            return (-1);
        }
	//if an fd is a member of the file descriptor set, assign read_fd to it 
        if (FD_ISSET(master_in_fifo, &read_fd_set)) {
            read_fd = master_in_fifo;
        } else if (FD_ISSET(left_in_fifo, &read_fd_set)) {
            read_fd = left_in_fifo;
        } else if (FD_ISSET(right_in_fifo, &read_fd_set)) {
            read_fd = right_in_fifo;
        } else {
            printf("Unknown read fd is set \n");
        }
	//empty buffer and read from selected fd
        memset(buf, 0x00, sizeof(buf));
        num_read = read(read_fd, buf, sizeof(buf));
        if (num_read < sizeof(POTATO_T)) {
	  //message is not a potato, its either a connecton message(startup protocol) or a signal to end game(zero recieved)
            code = atoi(buf);
            if (0 == code) {
                // shutdown
                close(master_out_fifo);
                close(master_in_fifo);
                close(left_out_fifo);
                close(left_in_fifo);
                close(right_out_fifo);
                close(right_in_fifo);
                return (0);
            } else {
                printf("Connected as player %d out of %d total players\n", player_num, code);
                num_players = code;
                // create specific player fifos, if the number of players is one, create a fifo that points to itself (p0_p0)
                snprintf(left_out_fifo_str, sizeof(left_out_fifo_str), "/tmp/p%d_p%d", player_num, (0 == player_num) ? (num_players - 1) : (player_num - 1));
                snprintf(right_out_fifo_str, sizeof(right_out_fifo_str), "/tmp/p%d_p%d", player_num, (num_players == (player_num +1)) ? 0 : (player_num + 1));
                snprintf(left_in_fifo_str, sizeof(left_in_fifo_str), "/tmp/p%d_p%d", (0 == player_num) ? (num_players - 1) : (player_num - 1), player_num);
                snprintf(right_in_fifo_str, sizeof(right_in_fifo_str), "/tmp/p%d_p%d", (num_players == (player_num + 1)) ? 0 : (player_num + 1), player_num);
		

		//sleep if this is the last player to connect
		//if(player_num == (num_players-1)) sleep(1);
		//open all fifos and simultaneously add them to the fd set,leave them open
		left_in_fifo = open(left_in_fifo_str,  O_RDWR);
                if (0 > left_in_fifo) {
		   printf("Failed to open %s fifo for reading \n", left_in_fifo_str);
		   return (-1);
                }
                FD_SET(left_in_fifo, &active_fd_set);

                right_in_fifo = open(right_in_fifo_str,  O_RDWR);
                if (0 > left_in_fifo) {
                    printf("Failed to open %s fifo for reading \n", right_in_fifo_str);
                    return (-1);
                }
                FD_SET(right_in_fifo, &active_fd_set);

                left_out_fifo = open(left_out_fifo_str,  O_RDWR);
                if (0 > left_out_fifo) {
                    printf("Failed to open %s fifo for reading \n", left_out_fifo_str);
                    return (-1);
                }

                right_out_fifo = open(right_out_fifo_str,  O_RDWR);
                if (0 > right_out_fifo) {
                    printf("Failed to open %s fifo for reading \n", right_out_fifo_str);
                    return (-1);
                }
            }

        } else if (num_read == sizeof(POTATO_T)) {//if a potato was recieved by a player
            potato_ptr = (POTATO_T*) buf;
	    //save the player that gets the potato as it gets it, then reduce hop_count
            potato_ptr->hop_trace[potato_ptr->total_hops - potato_ptr->hop_count] = (unsigned long) player_num;
            potato_ptr->hop_count--;
            if (0 == potato_ptr->hop_count) {
	      printf("I'm it\n");
                write(master_out_fifo, buf, sizeof(POTATO_T));
            } else {//send to either left or right
                next_player = rand() % 2;
                if (0 == next_player) {
                    write(left_out_fifo, buf, sizeof(POTATO_T));
                    next_player = (0 == player_num) ? (num_players - 1) : (player_num - 1);
                } else {
                    write(right_out_fifo, buf, sizeof(POTATO_T));
                    next_player = (num_players == (player_num + 1)) ? 0 : (player_num + 1);
                }
                printf("Sending potato to %d\n", next_player);
            }

        } else {//neither a potato nor startup message
            printf("Unknown message size %d expected %d\n", num_read, (int)sizeof(POTATO_T));
        }


    }
}

int main(int argc, char *argv[])
{
    int player_num;
    if (argc != 2) {
        printf("Wrong number of arguments \n");
        return (-1);
    }

    player_num = atoi(argv[1]);

    if(0 != play_game(player_num)) {
        printf("Failed to play the game\n");
        return (-1);
    }
}
