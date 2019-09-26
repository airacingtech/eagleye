/*
 * velocity_scale_factor.cpp
 * velocity_scale_factor estimate program
 * Author Sekino
 * Ver 1.00 2019/01/26
 */

#include "ros/ros.h"
#include "eagleye_msgs/VelocityScaleFactor.h"
#include "geometry_msgs/TwistStamped.h"
#include "rtklib_msgs/RtklibNav.h"
#include "xyz2enu_vel.hpp"

//default value
double estimated_number_min = 1000;
double estimated_number_max = 20000;
double estimated_velocity_threshold = 2.77;
double estimated_coefficient = 0.05;

bool gnss_status, estimate_start_status;
int i, tow_last, estimated_number;
double initial_velocity_scale_factor = 1.0;
double doppler_velocity = 0.0;
double raw_velocity_scale_factor = 0.0;
double velocity_scale_factor_last = 0.0;

std::size_t index_length;
std::size_t gnss_status_buffer_length;
std::vector<bool> gnss_status_buffer;
std::vector<double> doppler_velocity_buffer;
std::vector<double> velocity_buffer;

rtklib_msgs::RtklibNav rtklib_nav;

ros::Publisher pub;
eagleye_msgs::VelocityScaleFactor velocity_scale_factor;

void rtklib_nav_callback(const rtklib_msgs::RtklibNav::ConstPtr& msg)
{
  rtklib_nav.header = msg->header;
  rtklib_nav.tow = msg->tow;
  rtklib_nav.ecef_pos = msg->ecef_pos;
  rtklib_nav.ecef_vel = msg->ecef_vel;
  rtklib_nav.status = msg->status;

  double ecef_vel[3];
  double ecef_pos[3];
  double enu_vel[3];

  ecef_vel[0] = msg->ecef_vel.x;
  ecef_vel[1] = msg->ecef_vel.y;
  ecef_vel[2] = msg->ecef_vel.z;
  ecef_pos[0] = msg->ecef_pos.x;
  ecef_pos[1] = msg->ecef_pos.y;
  ecef_pos[2] = msg->ecef_pos.z;

  xyz2enu_vel(ecef_vel, ecef_pos, enu_vel);
  doppler_velocity = sqrt((enu_vel[0] * enu_vel[0]) + (enu_vel[1] * enu_vel[1]) + (enu_vel[2] * enu_vel[2]));
}

void velocity_callback(const geometry_msgs::TwistStamped::ConstPtr& msg)
{
  velocity_scale_factor.header = msg->header;

  if (estimated_number < estimated_number_max)
  {
    ++estimated_number;
  }
  else
  {
    estimated_number = estimated_number_max;
  }

  if (tow_last == rtklib_nav.tow)
  {
    gnss_status = false;
    //doppler_velocity = 0;
    tow_last = rtklib_nav.tow;
  }
  else
  {
    gnss_status = true;
    //doppler_velocity = doppler_velocity;
    tow_last = rtklib_nav.tow;
  }

  gnss_status_buffer.push_back(gnss_status);
  doppler_velocity_buffer.push_back(doppler_velocity);
  velocity_buffer.push_back(msg->twist.linear.x);

  gnss_status_buffer_length = std::distance(gnss_status_buffer.begin(), gnss_status_buffer.end());

  if (gnss_status_buffer_length > estimated_number_max)
  {
    gnss_status_buffer.erase(gnss_status_buffer.begin());
    doppler_velocity_buffer.erase(doppler_velocity_buffer.begin());
    velocity_buffer.erase(velocity_buffer.begin());
  }

  std::vector<int> gnss_index;
  std::vector<int> velocity_index;
  std::vector<int> index;
  std::vector<double> velocity_scale_factor_buffer;

  if (estimated_number > estimated_number_min && gnss_status_buffer[estimated_number - 1] == true && velocity_buffer[estimated_number - 1] > estimated_velocity_threshold)
  {
    for (i = 0; i < estimated_number; i++)
    {
      if (gnss_status_buffer[i] == true)
      {
        gnss_index.push_back(i);
      }
      if (velocity_buffer[i] > estimated_velocity_threshold)
      {
        velocity_index.push_back(i);
      }
    }

    set_intersection(gnss_index.begin(), gnss_index.end(), velocity_index.begin(), velocity_index.end(),
                     inserter(index, index.end()));

    index_length = std::distance(index.begin(), index.end());

    if (index_length > estimated_number * estimated_coefficient)
    {
      for (i = 0; i < index_length; i++)
      {
        velocity_scale_factor_buffer.push_back(doppler_velocity_buffer[index[i]] / velocity_buffer[index[i]]);
      }

      velocity_scale_factor.status.estimate_status = true;
      estimate_start_status = true;
    }
    else
    {
      velocity_scale_factor.status.estimate_status = false;
    }
  }
  else
  {
    velocity_scale_factor.status.estimate_status = false;
  }

  if (velocity_scale_factor.status.estimate_status == true)
  {
    // median
    size_t size = velocity_scale_factor_buffer.size();
    double* t = new double[size];
    std::copy(velocity_scale_factor_buffer.begin(), velocity_scale_factor_buffer.end(), t);
    std::sort(t, &t[size]);
    raw_velocity_scale_factor = size % 2 ? t[size / 2] : (t[(size / 2) - 1] + t[size / 2]) / 2;
    delete[] t;
    velocity_scale_factor.scale_factor = raw_velocity_scale_factor;
  }
  else if (velocity_scale_factor.status.estimate_status == false)
  {
    raw_velocity_scale_factor = 0;
    velocity_scale_factor.scale_factor = velocity_scale_factor_last;
  }

  if (estimate_start_status == true)
  {
    velocity_scale_factor.status.enabled_status = true;
    velocity_scale_factor.correction_velocity.twist.linear.x = msg->twist.linear.x * velocity_scale_factor.scale_factor;
  }
  else
  {
    velocity_scale_factor.status.enabled_status = false;
    velocity_scale_factor.correction_velocity.twist.linear.x = msg->twist.linear.x * initial_velocity_scale_factor;
  }

  pub.publish(velocity_scale_factor);
  velocity_scale_factor_last = velocity_scale_factor.scale_factor;

}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "velocity_scale_factor");
  ros::NodeHandle n;

  n.getParam("/eagleye/velocity_scale_factor/estimated_number_min",estimated_number_min);
  n.getParam("/eagleye/velocity_scale_factor/estimated_number_max",estimated_number_max);
  n.getParam("/eagleye/velocity_scale_factor/estimated_velocity_threshold",estimated_velocity_threshold);
  n.getParam("/eagleye/velocity_scale_factor/estimated_coefficient",estimated_coefficient);

  std::cout<< "estimated_number_min "<<estimated_number_min<<std::endl;
  std::cout<< "estimated_number_max "<<estimated_number_max<<std::endl;
  std::cout<< "estimated_velocity_threshold "<<estimated_velocity_threshold<<std::endl;
  std::cout<< "estimated_coefficient "<<estimated_coefficient<<std::endl;

  ros::Subscriber sub1 = n.subscribe("/can_twist", 1000, velocity_callback);
  ros::Subscriber sub2 = n.subscribe("/rtklib_nav", 1000, rtklib_nav_callback);
  pub = n.advertise<eagleye_msgs::VelocityScaleFactor>("/eagleye/velocity_scale_factor", 1000);

  ros::spin();

  return 0;
}