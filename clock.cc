#include "led-matrix.h"
#include "graphics.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>		//Needed for I2C port

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
#include <libconfig.h>

#include "smbus.h"

#define CONFIGFILE       "clock.conf"
#define CONFIGFILE_ETC   "/etc/clock.conf"
#define CONFIGFILE_LOCAL ".clock/clock.conf"

#ifndef IW_NAME
#define IW_NAME "wlan0"
#endif

#define BMP180_SENSOR
//#define DS18B20_SENSOR

const unsigned char BMP180_OVERSAMPLING_SETTING = 3;
const unsigned char BMP180_BUS = 1;

using namespace rgb_matrix;

char display_data[6][256] = { 0 } ;
pthread_mutex_t display_mutex;

char wifi_ssid[256];
char wifi_passwd[256] ;
char imap_url[256];
char imap_login[256];
char imap_passwd[256] ; 


struct curl_string {
  char *ptr;
  size_t len;
};

#ifdef BMP180_SENSOR
short int           ac1;
short int           ac2;
short int           ac3;
unsigned short int  ac4;
unsigned short int  ac5;
unsigned short int  ac6;
short int            b1;
short int            b2;
short int            mb;
short int            mc;
short int            md;

int                  b5;

unsigned int temperature, pressure;
#endif 


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
			case 'C':
				//Convert to Celcius
				tempC = tempC / 1000;
				//printf("Temp: %.3f C  ", tempC);
				break;
			case 'F':
				// Convert to Farenheit
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

#ifdef BMP180_SENSOR

int BMP180_i2c_Begin (void) {
	int file_i2c;
	char filename_i2c[256] = {0};

	sprintf( filename_i2c , "/dev/i2c-%d",BMP180_BUS);
	if ((file_i2c = open(filename_i2c, O_RDWR)) < 0)
	{
		//ERROR HANDLING: you can check errno to see what went wrong
		fprintf(stderr,"Failed to open the i2c bus %s : error : %d\n",filename_i2c,file_i2c);
		return -1;
	}

	int addr = 0x77;          //<<<<<The I2C address of the slave
	if (ioctl(file_i2c, I2C_SLAVE, addr) < 0)
	{
		fprintf(stderr,"Failed to acquire bus access and/or talk to slave.\n");
		//ERROR HANDLING; you can check errno to see what went wrong
		return -1;
	}

	return file_i2c ;
}

//Write a byte to the BMP085
void BMP180_i2c_Write_Byte(int fd, __u8 address, __u8 value)
{
   if (i2c_smbus_write_byte_data(fd, address, value) < 0) {
      close(fd);
      exit(1);
   }
}

long BMP180_i2c_Read_Int(int fd, unsigned char address)
{
	long res = i2c_smbus_read_word_data(fd, address);
	if (res < 0) {
		close(fd);
		exit(1);
	}

	// Convert result to 16 bits and swap bytes
	res = ((res<<8) & 0xFF00) | ((res>>8) & 0xFF);
	return res;
}

// Read a block of data BMP180
void BMP180_i2c_Read_Block(int fd, unsigned char address, unsigned char length, unsigned char  *values)
{
	if(i2c_smbus_read_i2c_block_data(fd, address,length,values)<0) {
		close(fd);
		exit(1);
	}
}


void BMP180_Calibration()
{
	int fd = BMP180_i2c_Begin();
	if ( fd >= 0) {
		ac1 = BMP180_i2c_Read_Int(fd,0xAA);
		ac2 = BMP180_i2c_Read_Int(fd,0xAC);
		ac3 = BMP180_i2c_Read_Int(fd,0xAE);
		ac4 = BMP180_i2c_Read_Int(fd,0xB0);
		ac5 = BMP180_i2c_Read_Int(fd,0xB2);
		ac6 = BMP180_i2c_Read_Int(fd,0xB4);
		b1  = BMP180_i2c_Read_Int(fd,0xB6);
		b2  = BMP180_i2c_Read_Int(fd,0xB8);
		mb  = BMP180_i2c_Read_Int(fd,0xBA);
		mc  = BMP180_i2c_Read_Int(fd,0xBC);
		md  = BMP180_i2c_Read_Int(fd,0xBE);
		close(fd);
	}
}

unsigned int BMP180_ReadUT()
{
	unsigned int ut = 0;
	int fd = BMP180_i2c_Begin();
        if ( fd >= 0) {
		// Write 0x2E into Register 0xF4
		// This requests a temperature reading
		BMP180_i2c_Write_Byte(fd,0xF4,0x2E);
		// Wait at least 4.5ms
		usleep(5000);
		// Read the two byte result from address 0xF6
		ut = BMP180_i2c_Read_Int(fd,0xF6);
		// Close the i2c file
		close (fd);
	}
	return ut;
}


// Read the uncompensated pressure value
unsigned int BMP180_ReadUP()
{
	unsigned int up = 0;
	int fd = BMP180_i2c_Begin();
        if ( fd >= 0) {
		// Write 0x34+(BMP180_OVERSAMPLING_SETTING<<6) into register 0xF4
		// Request a pressure reading w/ oversampling setting
		BMP180_i2c_Write_Byte(fd,0xF4,0x34 + (BMP180_OVERSAMPLING_SETTING<<6));
		// Wait for conversion, delay time dependent on oversampling setting
		usleep((2 + (3<<BMP180_OVERSAMPLING_SETTING)) * 1000);
		// Read the three byte result from 0xF6
		// 0xF6 = MSB, 0xF7 = LSB and 0xF8 = XLSB
   		unsigned char values[3];
		BMP180_i2c_Read_Block(fd, 0xF6, 3, values);
		up = (((unsigned int) values[0] << 16) | ((unsigned int) values[1] << 8) | (unsigned int) values[2]) >> (8-BMP180_OVERSAMPLING_SETTING);
	}
	return up;
}


unsigned int BMP180_GetPressure(unsigned int up)
{
	int x1, x2, x3, b3, b6, p;
	unsigned int b4, b7;

	b6 = b5 - 4000;
	// Calculate B3
	x1 = (b2 * (b6 * b6)>>12)>>11;
	x2 = (ac2 * b6)>>11;
	x3 = x1 + x2;
	b3 = (((((int)ac1)*4 + x3)<<BMP180_OVERSAMPLING_SETTING) + 2)>>2;

	// Calculate B4
	x1 = (ac3 * b6)>>13;
	x2 = (b1 * ((b6 * b6)>>12))>>16;
	x3 = ((x1 + x2) + 2)>>2;
	b4 = (ac4 * (unsigned int)(x3 + 32768))>>15;

	b7 = ((unsigned int)(up - b3) * (50000>>BMP180_OVERSAMPLING_SETTING));
	if (b7 < 0x80000000) {
		p = (b7<<1)/b4;
	} else {
		p = (b7/b4)<<1;
	}
	x1 = (p>>8) * (p>>8);
	x1 = (x1 * 3038)>>16;
	x2 = (-7357 * p)>>16;
	p += (x1 + x2 + 3791)>>4;

	return p;
}

// Calculate temperature given uncalibrated temperature
// Value returned will be in units of 0.1 deg C
unsigned int BMP180_GetTemperature(unsigned int ut)
{
	int x1, x2;

	x1 = (((int)ut - (int)ac6)*(int)ac5) >> 15;
	x2 = ((int)mc << 11)/(x1 + md);
	b5 = x1 + x2;

	unsigned int result = ((b5 + 8)>>4);

	return result;
}
#endif 

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
	Color color6(127,80,80);

	const char *bdf_font1_file = "/usr/lib/fonts/7x13B.bdf";
	const char *bdf_font2_file = "/usr/lib/fonts/4x6.bdf";
	const char *bdf_font3_file = "/usr/lib/fonts/6x12.bdf";

	int x_wifi_orig = 1;
	int y_wifi_orig = 1;
	int x_orig = 4;
	int y_orig = 8;
	int x_temp_orig =1;
	int y_temp_orig = 21;
	int x_pressure_orig = 1;
	int y_pressure_orig = 21;
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
		if ((tstruct.tm_sec & 1) == 0 ) {
                	strftime(display_data[0], sizeof(display_data[0]), "%H:%M:%S", &tstruct);
		} else {
                        strftime(display_data[0], sizeof(display_data[0]), "%H %M %S", &tstruct);
		}
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
#ifdef BMP180_SENSOR
			if ( loop >= 6) {
	                        rgb_matrix::DrawText(canvas, font3, x_pressure_orig, y_pressure_orig + font3.baseline(), color6, display_data[5]);
			} else {
	                        rgb_matrix::DrawText(canvas, font3, x_temp_orig, y_temp_orig + font3.baseline(), color3, display_data[2]);
			}
#else
			rgb_matrix::DrawText(canvas, font3, x_temp_orig, y_temp_orig + font3.baseline(), color3, display_data[2]);
#endif
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
		/* Set username and password*/
		curl_easy_setopt(curl, CURLOPT_USERNAME, imap_login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, imap_passwd);

		/* This is mailbox folder to select */
		curl_easy_setopt(curl, CURLOPT_URL, imap_url);

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

	config_t cfg;
        config_setting_t *root_cfg, *wifi_cfg , *imap_cfg;
	const char * envvar ; 


	config_init (&cfg);
	if (!config_read_file(&cfg, CONFIGFILE_LOCAL)) {
		if (!config_read_file(&cfg, CONFIGFILE_ETC)) {
	                if (!config_read_file(&cfg, CONFIGFILE)) {
				fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
				config_error_line(&cfg), config_error_text(&cfg));
				config_destroy(&cfg);
				exit ( 1 );
			}
		}
	}

	root_cfg = config_root_setting(&cfg);
	wifi_cfg = config_setting_get_member(root_cfg, "wifi");
        imap_cfg = config_setting_get_member(root_cfg, "imap");

	config_setting_lookup_string(wifi_cfg, "ssid", &envvar); strcpy (wifi_ssid,envvar);
        config_setting_lookup_string(wifi_cfg, "passwd", &envvar);strcpy (wifi_passwd,envvar);

        config_setting_lookup_string(imap_cfg, "url", &envvar);strcpy(imap_url,envvar);
        config_setting_lookup_string(imap_cfg, "login", &envvar);strcpy(imap_login,envvar);
        config_setting_lookup_string(imap_cfg, "passwd", &envvar);strcpy(imap_passwd,envvar);


	fprintf (stderr, "WIFI : %s - %s \n",wifi_ssid,wifi_passwd);
        fprintf (stderr, "IMAP : %s - %s - %s \n",imap_url,imap_login,imap_passwd);

	config_destroy(&cfg);

	led_options.chain_length = 2;
	runtime.drop_privileges = 1;


        RGBMatrix *pcanvas = rgb_matrix::CreateMatrixFromFlags(&argc, &argv,&led_options,&runtime);


	int  tempScale = 'C'; // Scale in degrees  C or F
	char dev[16];       // Dev ID for DS18B20 thermometer
	char devPath[128];  // Path to device
	float tTemp = 0;
	float tPressure = 0;
	int retval ;  
	int unread; 

	pthread_t thread1;

	if (pthread_create(&thread1, NULL, thread_display,(void*)pcanvas)) {
		perror("pthread_create");
		return EXIT_FAILURE;
    	}

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

#ifdef BMP180_SENSOR
		BMP180_Calibration();
		temperature = BMP180_GetTemperature(BMP180_ReadUT());
		tTemp = ((float) (temperature)) / 10 ;
		pressure = BMP180_GetPressure(BMP180_ReadUP());
		tPressure =  ((float) pressure ) / 100 ;
#endif

#ifdef DS18B20_SENSOR
    		tTemp = read_thermometer(devPath, tempScale);
#endif
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
		sprintf (display_data[5],"%.1f%s",tPressure, "hPa");
		sprintf (display_data[1],"WIFI : %s",essid);
		sprintf (display_data[3],"EMAIL: %d ",unread);
		pthread_mutex_unlock(&display_mutex);

	} while (1);
  	return 0;
}
