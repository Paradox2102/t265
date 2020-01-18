// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2019 Intel Corporation. All Rights Reserved.
#include <librealsense2/rs.hpp>
#include <iostream>
#include <iomanip>

// https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
#define _USE_MATH_DEFINES
#include <cmath>

const double degrees_per_radian = 180 / 3.14159265358979323;
const double inches_per_metre = 1 / 0.0254;

int main(int argc, char * argv[]) try
{
    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;
    // Create a configuration for configuring the pipeline with a non default profile
    rs2::config cfg;
    // Add pose stream
    cfg.enable_stream(RS2_STREAM_POSE, RS2_FORMAT_6DOF);
    // Start pipeline with chosen configuration
    pipe.start(cfg);

    // Main loop
    while (true)
    {
        // Wait for the next set of frames from the camera
        auto frames = pipe.wait_for_frames();
        // Get a frame from the pose stream
        auto f = frames.first_or_default(RS2_STREAM_POSE);
        // Cast the frame to pose_frame and get its data
        auto pose_data = f.as<rs2::pose_frame>().get_pose_data();

        double x = pose_data.translation.x * inches_per_metre;
        double y = -pose_data.translation.z * inches_per_metre;

	    double sinp = 2 * (pose_data.rotation.w * pose_data.rotation.y - 
	    	pose_data.rotation.z * pose_data.rotation.x);
    	double yaw = (std::abs(sinp) >= 1 ?
        	std::copysign(M_PI / 2, sinp) : std::asin(sinp))
        	* degrees_per_radian;

        std::cout << "\r" << "X=" << std::setprecision(1) << std::fixed 
              << std::setw(6) << x << "in, Y= " << std::setw(6) << y 
              << "in, Yaw=" << std::setw(6) << yaw << "deg"
              << ", TC=" << (int)pose_data.tracker_confidence
              << ", MC=" << (int)pose_data.mapper_confidence;
    }

    return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
