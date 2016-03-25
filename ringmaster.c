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
#define MAX_BUF 1024
#define WRITE_INTERVAL 500*1000

//structure for holding player attributes
typedef struct player {
    int  player_number;
    char out_fifo_str[50];
    char in_fifo_str[50];
    int  out_fifo_fd;
    int  in_fifo_fd;
    int  connected;
}player_t;

//Expectation is that player memory is allocated
int delete_fifos(int num_players, player_t * players)
{
    int i, ret = 0;;
    char out_fifo_left[50], out_fifo_right[50];

    for (i = 0; i < num_players; i++) {
        // do not care if unlink fails
        close(players[i].in_fifo_fd);
        if(0 != unlink(players[i].in_fifo_str)) {
            printf("Failed to unlink fifo %s\n", players[i].in_fifo_str);
            ret = -1;
        }

        close(players[i].out_fifo_fd);
        if(0 != unlink(players[i].out_fifo_str)) {
            printf("Failed to unlink fifo %s\n", players[i].out_fifo_str);
            ret = -1;
        }

        // unlink player fifos
        snprintf(out_fifo_right, sizeof(out_fifo_right), "/tmp/p%d_p%d", i, (num_players == (i+1)) ? 0 : (i+1));
        snprintf(out_fifo_left, sizeof(out_fifo_left), "/tmp/p%d_p%d", i, (0 == i) ? (num_players - 1) : (i-1));
        // do not care if unlink fails
        if(0 != unlink(out_fifo_right)) {
            printf("Failed to unlink fifo %s\n", out_fifo_right);
            ret = -1;
        }

        if(0 != unlink(out_fifo_left)) {
            printf("Failed to unlink fifo %s\n", out_fifo_left);
            ret = -1;
        }
    }
    return (ret);
}

//Expectation is that player memory is allocated
int create_fifos(int num_players, player_t * players)
{
    int i;
    char out_fifo_left[50], out_fifo_right[50];

    for (i = 0; i < num_players; i++) {
        // create master fifos
        snprintf(players[i].in_fifo_str, sizeof(players[i].in_fifo_str), "/tmp/p%d_master", i);
        snprintf(players[i].out_fifo_str, sizeof(players[i].out_fifo_str), "/tmp/master_p%d", i);
        // do not care if unlink fails
        unlink(players[i].in_fifo_str);
        unlink(players[i].out_fifo_str);
        if(0 != mkfifo(players[i].in_fifo_str, 0666)) {
            printf("Failed to create fifo %s\n", players[i].in_fifo_str);
            return (-1);
        }

        if(0 != mkfifo(players[i].out_fifo_str, 0666)) {
            printf("Failed to create fifo %s\n", players[i].out_fifo_str);
            return (-1);
        }

        // create player fifos
        snprintf(out_fifo_right, sizeof(out_fifo_right), "/tmp/p%d_p%d", i, (num_players == (i+1)) ? 0 : (i+1));
        snprintf(out_fifo_left, sizeof(out_fifo_left), "/tmp/p%d_p%d", i, (0 == i) ? (num_players -1) : (i-1));
        // do not care if unlink fails
        unlink(out_fifo_left);
        unlink(out_fifo_right);
        if(0 != mkfifo(out_fifo_left, 0666)) {
            printf("Failed to create fifo %s\n",out_fifo_left);
            return (-1);
        }

        if(0 != mkfifo(out_fifo_right, 0666)) {
            printf("Failed to create fifo %s\n",out_fifo_right);
            return (-1);
        }
    }
    return (0);
}

// expects memory to be allocated for the players
int open_fifos(int num_players, player_t * players, fd_set* active_fd_set)
{
    int i;
    FD_ZERO(active_fd_set);

    for (i = 0; i < num_players; i++) {
        // create master fifos
        players[i].in_fifo_fd = open(players[i].in_fifo_str,  O_RDWR);
        if (0 > players[i].in_fifo_fd) {
            printf("Failed to open %s fifo for reading \n", players[i].in_fifo_str);
            return (-1);
        }
        FD_SET(players[i].in_fifo_fd, active_fd_set);

        players[i].out_fifo_fd = open(players[i].out_fifo_str, O_RDWR);
        if (0 > players[i].out_fifo_fd) {
            printf("Failed to open %s fifo for writing \n", players[i].out_fifo_str);
            return (-1);
        }
    }
    return (0);
}

int all_players_connected(int num_players, player_t * players)
{
    int ret = 1, i;
    for (i = 0; i < num_players; i++) {
        if (1 != players[i].connected) {
            ret = 0;
            break;
        }
    }
    return (ret);
}

int send_shutdown(int num_players, player_t * players)
{
    int ret = 0, i;
    for (i = 0; i < num_players; i++) {
        if (1 == players[i].connected) {
            write(players[i].out_fifo_fd, "0", sizeof("0"));
        }
    }
    return (ret);
}

int play_game(int num_players, int num_hops, player_t * players, fd_set* active_fd_set)
{
    int i, hops;
    size_t size;
    char buf[2* sizeof(POTATO_T)];
    int num_read = 0;
    fd_set in_fd_set;
    int game_began = 0, start_player;
    POTATO_T potato, *potato_ptr;
    char num_players_str[50];

    snprintf(num_players_str, sizeof(num_players_str), "%d", num_players);

    // seed random generator;
    srand( (unsigned int) time(NULL) );

    while (1) {
        in_fd_set = *active_fd_set;
        /* Block until input arrives on one or more active sockets. */
        if (select(FD_SETSIZE, &in_fd_set, NULL, NULL, NULL) < 0) {
            printf("Failed to select\n");
            return (-1);
        }

        /* Service all the FDs with input pending. */
        for (i = 0; i < num_players; ++i) {
            if (FD_ISSET(players[i].in_fifo_fd, &in_fd_set)) {
                memset(buf, 0x00, sizeof(buf));
                num_read = read(players[i].in_fifo_fd, buf, sizeof(buf));

                if (sizeof(potato) > num_read) { //player is checking in
                    if (!players[i].connected) {
                        printf("Player %d is ready to play\n", i);
                        players[i].connected = 1;
                        write(players[i].out_fifo_fd, num_players_str, strlen(num_players_str) + 1);
			//sleep(1);
		    }
                    if (!game_began && all_players_connected(num_players, players)) {
		      //begin the game
		      sleep(1);
		      game_began = 1;
		      start_player = rand() % num_players;
		       
		       
			memset(&potato, 0x00, sizeof(potato));
                        
			/*potato.hop_count = rand() % (num_hops + 1);
                        potato.total_hops = potato.hop_count;
                        */
			potato.hop_count = num_hops;
			potato.total_hops = potato.hop_count;
			
			potato.msg_type = (char)1;

                        printf(" All players present, sending potato to player %d\n", start_player);

                        //send the potato if more than 0 hops
                        if(potato.hop_count) {
                            write(players[start_player].out_fifo_fd, &potato, sizeof(potato));
                        } else {
                            // Send shutdown to all players
                            send_shutdown(num_players, players);
                            return (0); // game over
                        }
                    }
                } else if (num_read == sizeof(potato)) {
                    //Must be game over
                    potato_ptr = (POTATO_T*) buf;
                    printf("Trace of potato:\n");
                    for (hops = 0; hops < potato_ptr ->total_hops; hops++) {
                        printf("%lu", potato_ptr->hop_trace[hops]);
			if (hops != potato_ptr->total_hops-1){
			  printf(",");
			}
		    }
                    printf("\n");

                    // Send shutdown to all players
                    send_shutdown(num_players, players);
                    return (0); // game over
                } else {
                    printf("Received message of unknown size %d from player %d\n", num_read, i);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int num_players, num_hops;
    fd_set active_fd_set;
    player_t *player_array;

    if (argc != 3) {
        printf("Wrong number of arguments \n");
        return (-1);
    }

    num_players = atoi(argv[1]);
    num_hops = atoi(argv[2]);
    if (num_players < 2) {
        printf("Number of players must be greater than 1 \n ");
        return (-1);
    }
    if ((num_hops < 0) || (num_hops > 512)) {
        printf("Number of hops must be between 0 and 512 inclusive \n");
        return (-1);
    }

    printf("Potato Ringmaster\n");
    printf("Players = %d\n", num_players);
    printf("Hops = %d\n", num_hops);

    player_array = malloc(num_players * sizeof(player_t));
    if (NULL == player_array) {
        printf("Failed to allocate memory for the players, exiting...\n");
        return (-1);
    }

    // create all of the fifos
    if (0 != create_fifos(num_players, player_array)) { // create
        printf("Failed to create fifos, exiting...\n");
        return (-1);
    }

    // open all of the fifos
    if (0 != open_fifos(num_players, player_array, &active_fd_set)) { // open
        printf("Failed to open fifos, exiting...\n");
        return (-1);
    }

    // open all of the fifos
    if (0 != play_game(num_players, num_hops, player_array, &active_fd_set)) { // open
        printf("Failed to play the game, exiting...\n");
        return (-1);
    }

    // shutdown
    if (0 != delete_fifos(num_players, player_array)) { // delete
        printf("Failed to delete fifos, please cleanup FS, exiting...\n");
        return (-1);
    }
}
