#include "led-matrix.h"
#include "graphics.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>


#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>

#include <curl/curl.h>

#ifndef IW_NAME
#define IW_NAME "wlan0"
#endif

using namespace rgb_matrix;

char display_data[5][256] = { 0 } ;
pthread_mutex_t display_mutex;


struct curl_string {
  char *ptr;
  size_t len;
};

static int usage(const char *progname) {
	fprintf(stderr, "usage: %s [options]\n", progname);
	return 1;
}

int init_thermometer(char *dpath, char *tdev) {
	DIR *dir;
	struct dirent *dirent; 
	char path[] = "/sys/bus/w1/devices";

	dir = opendir (path);
	if (dir != NULL){
		while ((dirent = readdir (dir)))
		// 1-wire devices are links beginning with 28-
		if (dirent->d_type == DT_LNK && strstr(dirent->d_name, "28-") != NULL) {
			strcpy(tdev, dirent->d_name);
		}
		(void) closedir (dir);
	}
	else{
		printf("Couldn't open the w1 devices directory");
		return 1;
	}
	// Assemble path to OneWire device
	sprintf(dpath, "%s/%s/w1_slave", path, tdev);
	return 0;
}

float read_thermometer(char *dpath, int scale)
{
	char buf[256];     // Data from device
	char tmpData[6];   // Temp C * 1000 reported by device
	float tempC = 0;
	ssize_t numRead;

	int fd = open(dpath, O_RDONLY);
	if(fd == -1){
		printf("Couldn't open the w1 device: %s\n.", dpath);
		return 1;  
  	}
	while((numRead = read(fd, buf, 256)) > 0) {
		strncpy(tmpData, strstr(buf, "t=") + 2, 5);
		tempC = strtof(tmpData, NULL);
		switch(toupper(scale)){
			case 'C':          //Convert to Celcius
				tempC = tempC / 1000;
				//printf("Temp: %.3f C  ", tempC);
				break;
			case 'F':          // Convert to Farenheit
				tempC =  ((tempC / 1000) * (9 / 5)) + 32;
				//printf("%.3f F\n\n", tempC);
				break;
			default:
				printf("Invalid Scale requested: %c\n",scale);
		}
	}
	close(fd);
	return tempC;
}


void *thread_display(void *arg)
{
	/* Pour enlever le warning */
	(void) arg;

	RGBMatrix *canvas = (RGBMatrix*) arg;

	// RGBMatrix *canvas = rgb_matrix::CreateMatrixFromFlags(&global_argc, &global_argv);
	Color color(0, 0, 255);
	Color color2(0, 127, 127);
	Color color3(127, 0, 255);
	Color color4(127,127,0);
	Color color5(127,127,127);

	const char *bdf_font1_file = "/usr/share/clock/fonts/7x13B.bdf";
	const char *bdf_font2_file = "/usr/share/clock/fonts/4x6.bdf";
	const char *bdf_font3_file = "/usr/share/clock/fonts/6x12.bdf";

	int x_wifi_orig = 1;
	int y_wifi_orig = 1;
	int x_orig = 4;
	int y_orig = 8;
	int x_temp_orig =34;
	int y_temp_orig = 21;
	int x_date_orig = 2;
	int y_date_orig = 21;
	int brightness = 50;


	rgb_matrix::Font font1;
	if (!font1.LoadFont(bdf_font1_file)) {
    		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font1_file);
		exit (1);
	}

	rgb_matrix::Font font2;
	if (!font2.LoadFont(bdf_font2_file)) {
		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font2_file);
		exit (1);
	}

	rgb_matrix::Font font3;
	if (!font3.LoadFont(bdf_font3_file)) {
		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font3_file);
		exit (1);
	}

	canvas->SetBrightness(brightness);

	bool all_extreme_colors = brightness == 100;
	all_extreme_colors &= color.r == 0 || color.r == 255;
	all_extreme_colors &= color.g == 0 || color.g == 255;
	all_extreme_colors &= color.b == 0 || color.b == 255;
	if (all_extreme_colors)
		canvas->SetPWMBits(1);
	int loop = 0 ; 
	do {
		sleep (1);
  
                time_t now = time(0);
                struct tm tstruct;
                tstruct = *localtime(&now);

		pthread_mutex_lock(&display_mutex);
                strftime(display_data[0], sizeof(display_data[0]), "%H:%M:%S", &tstruct);
                strftime(display_data[4], sizeof(display_data[4]), "%d/%m/%Y", &tstruct);

		canvas->Clear();
		rgb_matrix::DrawText(canvas, font1, x_orig, y_orig + font1.baseline(), color, display_data[0]);
		if (loop >=6) 
		{
			rgb_matrix::DrawText(canvas, font2, x_wifi_orig, y_wifi_orig + font2.baseline(), color2, display_data[1]);
		} else  {
                        rgb_matrix::DrawText(canvas, font2, x_wifi_orig, y_wifi_orig + font2.baseline(), color4, display_data[3]);
		}
		if ((loop >= 3) && ( loop < 9))
		{
			rgb_matrix::DrawText(canvas, font3, x_temp_orig, y_temp_orig + font3.baseline(), color3, display_data[2]);
		} else {
                        rgb_matrix::DrawText(canvas, font3, x_date_orig, y_date_orig + font3.baseline(), color5, display_data[4]);
		}
	    	pthread_mutex_unlock(&display_mutex);
		loop ++ ; 
		if (loop > 12) loop = 0 ; 

    } while (1); 
        canvas->Clear();
        delete canvas;

	pthread_exit(NULL);
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct curl_string *s)
{
  	size_t new_len = s->len + size*nmemb;

	s->ptr = (char *)  realloc(s->ptr, new_len+1);
	if (s->ptr == NULL) {
		fprintf(stderr, "realloc() failed\n");
		exit(EXIT_FAILURE);
  	}
	memcpy(s->ptr+s->len, ptr, size*nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;

	return size*nmemb;
}

void curl_init_string(struct curl_string *s) {
	s->len = 0;
	s->ptr = (char *) malloc(s->len+1);
	if (s->ptr == NULL) {
		fprintf(stderr, "malloc() failed\n");
		exit(EXIT_FAILURE);
	}
	s->ptr[0] = '\0';
}


int unreadmail (void)
{
	CURL *curl;
	CURLcode res = CURLE_OK;
	int count = 0 ; 	
	curl = curl_easy_init();
	if(curl) {
		struct curl_string s;
		curl_init_string(&s);
		/* Set username and password */
		curl_easy_setopt(curl, CURLOPT_USERNAME, "<login>");
		curl_easy_setopt(curl, CURLOPT_PASSWORD, "<password>");

		/* This is mailbox folder to select */
		curl_easy_setopt(curl, CURLOPT_URL, "imap/<ip>/Inbox");

		/* Set the SEARCH command specifing what we want to search for. Note that
		 * this can contain a message sequence set and a number of search criteria
		 * keywords including flags such as ANSWERED, DELETED, DRAFT, FLAGGED, NEW,
		 * RECENT and SEEN. For more information about the search criteria please
		 * see RFC-3501 section 6.4.4.   */
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "SEARCH UnSeen");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

		/* Perform the custom request */
		res = curl_easy_perform(curl);

    		// printf("CURL STRING : %s\n", s.ptr);

		char * ptroffset = s.ptr ;
		count = 0 ; 
		while ( strstr  (ptroffset," ") != NULL )
		{
			ptroffset =  strstr  (ptroffset," ")  + 1 ; 
			count ++ ; 
		}
		if (count > 0) count -- ; 
    		free(s.ptr);

		/* Check for errors */
		if(res != CURLE_OK)
			 fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));

		/* Always cleanup */
		curl_easy_cleanup(curl);
  	}
	if (res != 0) count = 0 ; 
	return (int)count ;

}

int main(int argc, char *argv[]) {

	RGBMatrix::Options led_options ;
        rgb_matrix::RuntimeOptions runtime;

	led_options.chain_length = 2;
	runtime.drop_privileges = 1;


        RGBMatrix *pcanvas = rgb_matrix::CreateMatrixFromFlags(&argc, &argv,&led_options,&runtime);


	int  tempScale = 'C'; // Scale in degrees  C or F
	char dev[16];       // Dev ID for DS18B20 thermometer
	char devPath[128];  // Path to device
	float tTemp = 0;
	int retval ;  
	int unread; 

	pthread_t thread1;

	if (pthread_create(&thread1, NULL, thread_display,(void*)pcanvas)) {
		perror("pthread_create");
		return EXIT_FAILURE;
    	}
/*
	if (pthread_join(thread1, NULL)) {
		perror("pthread_join");
		return EXIT_FAILURE;
	}
*/
	retval = init_thermometer(devPath, dev);
	if (retval != 0) {
		fprintf (stderr , "Warning : Enable to read temp \n");
	}

	int opt;
	while ((opt = getopt(argc, argv, "x:y:f:C:b:")) != -1) {
  		switch (opt) {
			default:
				return usage(argv[0]);
		}
	}

	do {
  
    		tTemp = read_thermometer(devPath, tempScale);
		unread = unreadmail ();

		int sockfd;
		struct iw_statistics stats;
		struct iwreq req;
		memset(&stats, 0, sizeof(stats));
		memset(&req, 0, sizeof(iwreq));
		strcpy(req.ifr_name,IW_NAME);
		req.u.data.pointer = &stats;
		req.u.data.length = sizeof(iw_statistics);
#ifdef CLEAR_UPDATED
		req.u.data.flags = 1;
#endif

		/* Any old socket will do, and a datagram socket is pretty cheap */
		if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
			perror("Could not create simple datagram socket");
			exit(EXIT_FAILURE);
		}

		/* Perform the ioctl */
		if(ioctl(sockfd, SIOCGIWSTATS, &req) == -1) {
			perror("Error performing SIOCGIWSTATS on " IW_NAME);
			close(sockfd);
			exit(EXIT_FAILURE);
		}

		char essid[256];
		memset (essid,0,256);
		req.u.essid.pointer = (char *) essid ;
		/* Perform the ioctl */
		if(ioctl(sockfd, SIOCGIWESSID , &req) == -1) {
			perror("Error performing SIOCGIWSTATS on " IW_NAME);
			close(sockfd);
			exit(EXIT_FAILURE);
		}
		close(sockfd);

		pthread_mutex_lock(&display_mutex);
                sprintf (display_data[2],"%.1f%c",tTemp, tempScale);
		sprintf (display_data[1],"WIFI : %s",essid);
		sprintf (display_data[3],"EMAIL: %d ",unread);
		pthread_mutex_unlock(&display_mutex);

	} while (1);
  	return 0;
}
