#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include "opencv2/gpu/gpu.hpp"
#include "opencv2/highgui/highgui.hpp"
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#define RED     CV_RGB(255, 0, 0)
#define GREEN   CV_RGB(0, 255, 0)
#define BLUE    CV_RGB(0, 0, 255)
#define YELLOW  CV_RGB(255, 255, 0)
#define PURPLE  CV_RGB(255, 0, 255)
#define GREY    CV_RGB(200, 200, 200)
#define NUM_FINGERS	5
#define NUM_DEFECTS	8
#define VIDEO_FILE	"video.avi"
#define VIDEO_FORMAT	CV_FOURCC('M', 'J', 'P', 'G')
#define SERVER_PORT htons(50007)

using namespace std;
using namespace cv;

bool help_showed = false;
int xpos = 0;
int ypos = 0;

class Args
{
public:
    Args();
    static Args read(int argc, char** argv);

    string src;
    bool src_is_video;
    bool src_is_camera;
    int camera_id;

    bool write_video;
    string dst_video;
    double dst_video_fps;

    bool make_gray;

    bool resize_src;
    int width, height;

    double scale;
    int nlevels;
    int gr_threshold;

    double hit_threshold;
    bool hit_threshold_auto;

    int win_width;
    int win_stride_width, win_stride_height;

    bool gamma_corr;
};


class App
{
public:
    App(const Args& s);
    void run(struct ctx *ctx);

    void handleKey(char key);

    void hogWorkBegin();
    void hogWorkEnd();
    string hogWorkFps() const;

    void workBegin();
    void workEnd();
    string workFps() const;

    string message() const;

private:
    App operator=(App&);

    Args args;
    bool running;

    bool use_gpu;
    bool make_gray;
    double scale;
    int gr_threshold;
    int nlevels;
    double hit_threshold;
    bool gamma_corr;

    int64 hog_work_begin;
    double hog_work_fps;

    int64 work_begin;
    double work_fps;
};

static void printHelp()
{
    cout << "Histogram of Oriented Gradients descriptor and detector sample.\n"
         << "\nUsage: hog_gpu\n"
         << "  (<image>|--video <vide>|--camera <camera_id>) # frames source\n"
         << "  [--make_gray <true/false>] # convert image to gray one or not\n"
         << "  [--resize_src <true/false>] # do resize of the source image or not\n"
         << "  [--width <int>] # resized image width\n"
         << "  [--height <int>] # resized image height\n"
         << "  [--hit_threshold <double>] # classifying plane distance threshold (0.0 usually)\n"
         << "  [--scale <double>] # HOG window scale factor\n"
         << "  [--nlevels <int>] # max number of HOG window scales\n"
         << "  [--win_width <int>] # width of the window (48 or 64)\n"
         << "  [--win_stride_width <int>] # distance by OX axis between neighbour wins\n"
         << "  [--win_stride_height <int>] # distance by OY axis between neighbour wins\n"
         << "  [--gr_threshold <int>] # merging similar rects constant\n"
         << "  [--gamma_correct <int>] # do gamma correction or not\n"
         << "  [--write_video <bool>] # write video or not\n"
         << "  [--dst_video <path>] # output video path\n"
         << "  [--dst_video_fps <double>] # output video fps\n";
    help_showed = true;
}

struct ctx {
	CvCapture	*capture;	/* Capture handle */
	CvVideoWriter	*writer;	/* File recording handle */

	Mat	image;		/* Input image */
	Mat	thr_image;	/* After filtering and thresholding */
	Mat	temp_image1;	/* Temporary image (1 channel) */
	Mat	temp_image3;	/* Temporary image (3 channels) */
	
	gpu::GpuMat	GPU_image;		/* Input image */
	gpu::GpuMat	GPU_thr_image;	/* After filtering and thresholding */
	gpu::GpuMat	GPU_temp_image1;	/* Temporary image (1 channel) */
	gpu::GpuMat	GPU_temp_image3;	/* Temporary image (3 channels) */

	std::vector<cv::Point>		contour;	/* Hand contour */
	std::vector<cv::Point>		hull;		/* Hand convex hull */

	CvPoint		hand_center;
	CvPoint		*fingers;	/* Detected fingers positions */
	CvPoint		*defects;	/* Convexity defects depth points */

	CvMemStorage	*hull_st;
	CvMemStorage	*contour_st;
	CvMemStorage	*temp_st;
	CvMemStorage	*defects_st;

    Mat	kernel;	/* Kernel for morph operations */

	int		num_fingers;
	int		hand_radius;
	int		num_defects;
};

void init_ctx(struct ctx *ctx)
{
	//ctx->thr_image = //(gpu::GpuMat*)gpu::GpuMat(cvGetSize(ctx->image), CV_8UC1); 
	ctx->thr_image = Mat(cvCreateImage(ctx->image.size(), 8, 1));
	ctx->temp_image1 = Mat(cvCreateImage(ctx->image.size(), 8, 1));
	ctx->temp_image3 = Mat(cvCreateImage(ctx->image.size(), 8, 3));
	
	ctx->GPU_thr_image = gpu::GpuMat(cvCreateImage(ctx->GPU_image.size(), 8, 1));
	ctx->GPU_temp_image1 = gpu::GpuMat(cvCreateImage(ctx->GPU_image.size(), 8, 1));
	ctx->GPU_temp_image3 = gpu::GpuMat(cvCreateImage(ctx->GPU_image.size(), 8, 3));
	
	ctx->kernel = getStructuringElement(MORPH_RECT, Size(9,9), Point(0,0));
	
	ctx->contour_st = cvCreateMemStorage(0);
	ctx->hull_st = cvCreateMemStorage(0);
	ctx->temp_st = cvCreateMemStorage(0);
	ctx->fingers = (CvPoint *)calloc(NUM_FINGERS + 1, sizeof(CvPoint));
	ctx->defects = (CvPoint *)calloc(NUM_DEFECTS, sizeof(CvPoint));
}

void filter_and_threshold(struct ctx *ctx)
{
    
	/* Soften image */

	//gpu::GaussianBlur(ctx->GPU_image, ctx->GPU_temp_image3, Size(11,11), 0, 0);
	//GaussianBlur(ctx->image, ctx->temp_image3, Size(11,11), 0, 0);
	
	/* Remove some impulsive noise */
	//gpu::medianBlur(ctx->GPU_temp_image3,ctx->GPU_temp_image3, 11);

    //gpu::cvtColor(ctx->GPU_temp_image3, ctx->GPU_temp_image3, CV_BGR2HSV);
    cvtColor(ctx->image, ctx->temp_image3, CV_BGR2HSV);
	//cvCvtColor(ctx->temp_image3, ctx->temp_image3, CV_BGR2HSV);

	/*
	 * Apply threshold on HSV values to detect skin color
	 */
	 
	 //ctx->GPU_temp_image3.download(ctx->temp_image3);
	 
	inRange(ctx->temp_image3, Scalar(0, 55, 90, 255), Scalar(28, 175, 230, 255), ctx->thr_image);
		   
    ctx->GPU_thr_image.upload(ctx->thr_image);
    
    gpu::GaussianBlur(ctx->GPU_thr_image, ctx->GPU_thr_image, Size(11,11), 0, 0);

	/* Apply morphological opening */
	gpu::morphologyEx(ctx->GPU_thr_image, ctx->GPU_thr_image, MORPH_OPEN, ctx->kernel, Point(0,0), 1);
	//cvSmooth(ctx->thr_image, ctx->thr_image, CV_GAUSSIAN, 3, 3, 0, 0);
	gpu::GaussianBlur(ctx->GPU_thr_image, ctx->GPU_thr_image, Size(3,3), 0, 0);
	
	ctx->GPU_thr_image.download(ctx->thr_image);
}

void find_contour(struct ctx *ctx)
{
	double area, max_area = 0.0;
	//CvSeq *contours, *tmp, *contour = NULL;
	std::vector<std::vector<cv::Point> > contours2;
	std::vector<cv::Point> contour;
	//Mat 

	/* cvFindContours modifies input image, so make a copy */
	//cvCopy(ctx->thr_image, ctx->temp_image1, NULL);
	ctx->thr_image.copyTo(ctx->temp_image1);
	findContours(ctx->temp_image1, contours2, CV_RETR_EXTERNAL,
		       CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));

    int i = 0;
	// Select contour having greatest area
	for(std::vector<std::vector<cv::Point> >::iterator it = contours2.begin(); it !=contours2.end(); ++it, ++i) {
	    area = fabs(contourArea(contours2.at(i)));
	    if (area > max_area) {
			max_area = area;
			contour = contours2.at(i);
		}
	}
	/*
	for (tmp = contours; tmp; tmp = tmp->h_next) {
		area = fabs(cvContourArea(tmp, CV_WHOLE_SEQ, 0));
		if (area > max_area) {
			max_area = area;
			contour = tmp;
		}
	}*/

	// Approximate contour with poly-line
	
	if (contour.begin() != contour.end()) {
		approxPolyDP(Mat(contour), ctx->contour, 2, 1);
		//ctx->contour = contour;
	}
}

char buffer[1000];
        int n;

        int serverSock;
        int clientSock;

int main(int argc, char** argv)
{
    struct ctx ctx = { };
    
    serverSock=socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = SERVER_PORT;
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        // bind (this socket, local address, address length)
        //   bind server socket (serverSock) to server address (serverAddr).  
        //   Necessary so that server can use a specific port 
        bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(struct sockaddr));

        // wait for a client
        // listen (this socket, request queue length) 
        listen(serverSock,5);
        
        sockaddr_in clientAddr;
        socklen_t sin_size=sizeof(struct sockaddr_in);
        clientSock=accept(serverSock,(struct sockaddr*)&clientAddr, &sin_size);
        
    try
    {
        if (argc < 2)
            printHelp();
        Args args = Args::read(argc, argv);
        if (help_showed)
            return -1;
        App app(args);
        app.run(&ctx);
    }    
    catch (const Exception& e) { return cout << "error: "  << e.what() << endl, 1; }
    catch (const exception& e) { return cout << "error: "  << e.what() << endl, 1; }
    catch(...) { return cout << "unknown exception" << endl, 1; }
    
    close(serverSock);
    return 0;
}


Args::Args()
{
    src_is_video = false;
    src_is_camera = false;
    camera_id = 0;

    write_video = false;
    dst_video_fps = 24.;

    make_gray = false;

    resize_src = false;
    width = 640;
    height = 480;

    scale = 1.05;
    nlevels = 13;
    gr_threshold = 8;
    hit_threshold = 1.4;
    hit_threshold_auto = true;

    win_width = 48;
    win_stride_width = 8;
    win_stride_height = 8;

    gamma_corr = true;
}


Args Args::read(int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; i++)
    {
        if (string(argv[i]) == "--make_gray") args.make_gray = (string(argv[++i]) == "true");
        else if (string(argv[i]) == "--resize_src") args.resize_src = (string(argv[++i]) == "true");
        else if (string(argv[i]) == "--width") args.width = atoi(argv[++i]);
        else if (string(argv[i]) == "--height") args.height = atoi(argv[++i]);
        else if (string(argv[i]) == "--hit_threshold")
        {
            args.hit_threshold = atof(argv[++i]);
            args.hit_threshold_auto = false;
        }
        else if (string(argv[i]) == "--scale") args.scale = atof(argv[++i]);
        else if (string(argv[i]) == "--nlevels") args.nlevels = atoi(argv[++i]);
        else if (string(argv[i]) == "--win_width") args.win_width = atoi(argv[++i]);
        else if (string(argv[i]) == "--win_stride_width") args.win_stride_width = atoi(argv[++i]);
        else if (string(argv[i]) == "--win_stride_height") args.win_stride_height = atoi(argv[++i]);
        else if (string(argv[i]) == "--gr_threshold") args.gr_threshold = atoi(argv[++i]);
        else if (string(argv[i]) == "--gamma_correct") args.gamma_corr = (string(argv[++i]) == "true");
        else if (string(argv[i]) == "--write_video") args.write_video = (string(argv[++i]) == "true");
        else if (string(argv[i]) == "--dst_video") args.dst_video = argv[++i];
        else if (string(argv[i]) == "--dst_video_fps") args.dst_video_fps = atof(argv[++i]);
        else if (string(argv[i]) == "--help") printHelp();
        else if (string(argv[i]) == "--video") { args.src = argv[++i]; args.src_is_video = true; }
        else if (string(argv[i]) == "--camera") { args.camera_id = atoi(argv[++i]); args.src_is_camera = true; }
        else if (args.src.empty()) args.src = argv[i];
        else throw runtime_error((string("unknown key: ") + argv[i]));
    }
    return args;
}


App::App(const Args& s)
{
    cv::gpu::printShortCudaDeviceInfo(cv::gpu::getDevice());

    args = s;
    cout << "\nControls:\n"
         << "\tESC - exit\n"
         << "\tm - change mode GPU <-> CPU\n"
         << "\tg - convert image to gray or not\n"
         << "\t1/q - increase/decrease HOG scale\n"
         << "\t2/w - increase/decrease levels count\n"
         << "\t3/e - increase/decrease HOG group threshold\n"
         << "\t4/r - increase/decrease hit threshold\n"
         << endl;

    use_gpu = true;
    make_gray = args.make_gray;
    scale = args.scale;
    gr_threshold = args.gr_threshold;
    nlevels = args.nlevels;

    if (args.hit_threshold_auto)
        args.hit_threshold = args.win_width == 48 ? 1.4 : 0.;
    hit_threshold = args.hit_threshold;

    gamma_corr = args.gamma_corr;

    if (args.win_width != 64 && args.win_width != 48)
        args.win_width = 64;

    cout << "Scale: " << scale << endl;
    if (args.resize_src)
        cout << "Resized source: (" << args.width << ", " << args.height << ")\n";
    cout << "Group threshold: " << gr_threshold << endl;
    cout << "Levels number: " << nlevels << endl;
    cout << "Win width: " << args.win_width << endl;
    cout << "Win stride: (" << args.win_stride_width << ", " << args.win_stride_height << ")\n";
    cout << "Hit threshold: " << hit_threshold << endl;
    cout << "Gamma correction: " << gamma_corr << endl;
    cout << endl;
}


void App::run(struct ctx *ctx)
{
    running = true;
    cv::VideoWriter video_writer;

    Size win_size(args.win_width, args.win_width * 2); //(64, 128) or (48, 96)
    Size win_stride(args.win_stride_width, args.win_stride_height);

    while (running)
    {
        VideoCapture vc;
        Mat frame;

        if (args.src_is_camera)
        {
            vc.open(args.camera_id);
            if (!vc.isOpened())
            {
                stringstream msg;
                msg << "can't open camera: " << args.camera_id;
                throw runtime_error(msg.str());
            }
            vc >> frame;
        }
        else
        {
            frame = imread(args.src);
            if (frame.empty())
                throw runtime_error(string("can't open image file: " + args.src));
        }

        Mat img_aux, img, img_to_show;
        gpu::GpuMat gpu_img;

        // Iterate over all frames
        while (running && !frame.empty())
        {
            workBegin();

            // Change format of the image
            if (make_gray) cvtColor(frame, img_aux, CV_BGR2GRAY);
            else if (use_gpu) cvtColor(frame, img_aux, CV_BGR2BGRA);
            else frame.copyTo(img_aux);

            // Resize image
            if (args.resize_src) resize(img_aux, img, Size(args.width, args.height));
            else resize(img_aux, img, Size(180, 120));//img = img_aux;
            img_to_show = img_aux;
/*
            gpu_hog.nlevels = nlevels;
            cpu_hog.nlevels = nlevels;
*/
            vector<Rect> found;

            // Perform HOG classification
            //hogWorkBegin();
            if (use_gpu)
            {
                gpu_img.upload(img);
                //gpu_hog.detectMultiScale(gpu_img, found, hit_threshold, win_stride,
                //                         Size(0, 0), scale, gr_threshold);
                init_ctx(ctx);
                ctx->image = img;
                ctx->GPU_image = gpu_img;
                
                filter_and_threshold(ctx);
                //ctx->thr_image = ctx->image;
		        find_contour(ctx);
		        //find_convex_hull(ctx);
		        //find_fingers(ctx);
		        
		        Moments moment; 

                moment = moments(ctx->contour); 
                 
                int xposcurr = (int)(moment.m10/moment.m00); 
                int yposcurr = (int)(moment.m01/moment.m00); 
                
                if(fabs(xposcurr - xpos) < 30 || xpos == 0)
                    xpos = xposcurr;
                if(fabs(yposcurr - ypos) < 30 || ypos == 0)
                    ypos = yposcurr;   
                    
                cout << ypos;
                cout << "\nX Pos: ";
                cout << xpos;
                cout << "\n\nY Pos: ";
                
                
                //bzero(buffer, 1000);

				int32_t conv = htonl(ypos);
				char *data = (char*)&conv;
				int left = sizeof(conv);
				int rc;
				while (left) {
					rc = write(clientSock, data, 4);
					//if (rc < 0) return -1;
					left -= rc;
				}
				
				int32_t conv2 = htonl(xpos);
				char *data2 = (char*)&conv2;
				int left2 = sizeof(conv2);
				int rc2;
				while (left2) {
					rc2 = write(clientSock, data2, 4);
					//if (rc < 0) return -1;
					left2 -= rc2;
				}
                

                //receive a message from a client
               // n = read(clientSock, buffer, 500);
                
               // string s = string(ypos);

               // strcpy(buffer, s.c_str());
               // n = write(clientSock, ypos, strlen(buffer));
                
                //strcpy(buffer, to_string(xpos).c_str());
               // n = write(clientSock, xpos, strlen(buffer));
                
                

            }
            //else cpu_hog.detectMultiScale(img, found, hit_threshold, win_stride,
            //                              Size(0, 0), scale, gr_threshold);
            //hogWorkEnd();

            // Draw positive classified windows
            for (size_t i = 0; i < found.size(); i++)
            {
                Rect r = found[i];
                rectangle(img_to_show, r.tl(), r.br(), CV_RGB(0, 255, 0), 3);
            }

            if (use_gpu)
                putText(img_to_show, "Mode: GPU", Point(5, 25), FONT_HERSHEY_SIMPLEX, 1., Scalar(255, 100, 0), 2);
            else
                putText(img_to_show, "Mode: CPU", Point(5, 25), FONT_HERSHEY_SIMPLEX, 1., Scalar(255, 100, 0), 2);
            //putText(img_to_show, "FPS (HOG only): " + hogWorkFps(), Point(5, 65), FONT_HERSHEY_SIMPLEX, 1., Scalar(255, 100, 0), 2);
            putText(img_to_show, "FPS (total): " + workFps(), Point(5, 105), FONT_HERSHEY_SIMPLEX, 1., Scalar(255, 100, 0), 2);
            imshow("opencv_gpu_hog", img_to_show);

            if (args.src_is_video || args.src_is_camera) vc >> frame;

            workEnd();

            waitKey(1);
            //handleKey((char)waitKey(3));
        }
    }
}


void App::handleKey(char key)
{
    switch (key)
    {
    case 27:
        running = false;
        break;
    case 'm':
    case 'M':
        use_gpu = !use_gpu;
        cout << "Switched to " << (use_gpu ? "CUDA" : "CPU") << " mode\n";
        break;
    case 'g':
    case 'G':
        make_gray = !make_gray;
        cout << "Convert image to gray: " << (make_gray ? "YES" : "NO") << endl;
        break;
    case '1':
        scale *= 1.05;
        cout << "Scale: " << scale << endl;
        break;
    case 'q':
    case 'Q':
        scale /= 1.05;
        cout << "Scale: " << scale << endl;
        break;
    case '2':
        nlevels++;
        cout << "Levels number: " << nlevels << endl;
        break;
    case 'w':
    case 'W':
        nlevels = max(nlevels - 1, 1);
        cout << "Levels number: " << nlevels << endl;
        break;
    case '3':
        gr_threshold++;
        cout << "Group threshold: " << gr_threshold << endl;
        break;
    case 'e':
    case 'E':
        gr_threshold = max(0, gr_threshold - 1);
        cout << "Group threshold: " << gr_threshold << endl;
        break;
    case '4':
        hit_threshold+=0.25;
        cout << "Hit threshold: " << hit_threshold << endl;
        break;
    case 'r':
    case 'R':
        hit_threshold = max(0.0, hit_threshold - 0.25);
        cout << "Hit threshold: " << hit_threshold << endl;
        break;
    case 'c':
    case 'C':
        gamma_corr = !gamma_corr;
        cout << "Gamma correction: " << gamma_corr << endl;
        break;
    }
}


inline void App::hogWorkBegin() { hog_work_begin = getTickCount(); }

inline void App::hogWorkEnd()
{
    int64 delta = getTickCount() - hog_work_begin;
    double freq = getTickFrequency();
    hog_work_fps = freq / delta;
}

inline string App::hogWorkFps() const
{
    stringstream ss;
    ss << hog_work_fps;
    return ss.str();
}


inline void App::workBegin() { work_begin = getTickCount(); }

inline void App::workEnd()
{
    int64 delta = getTickCount() - work_begin;
    double freq = getTickFrequency();
    work_fps = freq / delta;
}

inline string App::workFps() const
{
    stringstream ss;
    ss << work_fps;
    return ss.str();
}
