#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

	// start in lane 1
	int lane = 1;

	// have a reference velocity to target
	double ref_vel = 0.0;  //mph

  h.onMessage([&ref_vel,&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
					double car_x = j[1]["x"];
					double car_y = j[1]["y"];
					double car_s = j[1]["s"];
					double car_d = j[1]["d"];
					double car_yaw = j[1]["yaw"];
					double car_speed = j[1]["speed"];

					// Previous path data given to the Planner
					auto previous_path_x = j[1]["previous_path_x"];
					auto previous_path_y = j[1]["previous_path_y"];
					// Previous path's end s and d values
					double end_path_s = j[1]["end_path_s"];
					double end_path_d = j[1]["end_path_d"];

					// Sensor Fusion Data, a list of all other cars on the same side of the road.
					auto sensor_fusion = j[1]["sensor_fusion"];

					// previous size
					int prev_size = previous_path_x.size();



          // aviod hitting the car in front of us
          if(prev_size > 0)
          {
            car_s = end_path_s;
          }

          bool too_close = false;
          int old_lane=lane;

          double safe_ahead = 15;
          double safe_behind = -10;

//          double right_behind_s;
//          double right_ahead_s;
//          double far_right_behind_s;
//          double far_right_ahead_s;
//          double left_behind_s;
//          double left_ahead_s;
//          double far_left_behind_s;
//          double far_left_ahead_s;
          double first_behind_s;
          double second_behind_s;
          double third_behind_s;
          double first_ahead_s;
          double second_ahead_s;
          double third_ahead_s;

          // find ref_v to use
          for(int i=0; i<sensor_fusion.size(); i++)
          {
            // car is in my lane
            float d = sensor_fusion[i][6];
            if(d < (2+4*lane+2) && d > (2+4*lane-2))
            {
              double vx = sensor_fusion[i][3];
              double vy = sensor_fusion[i][4];
              double check_speed = sqrt(vx * vx + vy * vy);
              double check_car_s = sensor_fusion[i][5];

              check_car_s += ((double) prev_size * 0.02 * check_speed);
              if ((check_car_s > car_s) && ((check_car_s - car_s) < 30))
              {
//                ref_vel = 29.5; //mph
                too_close = true;
              }
            }
          }

          if (too_close)
          {
//            ref_vel -= 0.224;
            ref_vel -= 0.15;
            // plan for next move
            first_behind_s = -100;
            second_behind_s = -100;
            third_behind_s = -100;
            first_ahead_s = 100;
            second_ahead_s = 100;
            third_ahead_s = 100;
            for(int i=0; i<sensor_fusion.size(); i++)
            {
              float d = sensor_fusion[i][6];

              if(d < (2+4*0+2) && d > (2+4*0-2))
              {
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx * vx + vy * vy);
                double check_car_s = sensor_fusion[i][5];
                check_car_s += ((double) prev_size * 0.02 * check_speed);

                if(check_car_s > car_s)
                  // ahead
                {
                  if(check_car_s - car_s < first_ahead_s)
                  {
                    first_ahead_s = check_car_s - car_s;
                  }
                } else
                  // behind
                {
                  if(check_car_s - car_s > first_behind_s)
                  {
                    first_behind_s = check_car_s - car_s;
                  }
                }
              } else if(d < (2+4*1+2) && d > (2+4*1-2))
              {
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx * vx + vy * vy);
                double check_car_s = sensor_fusion[i][5];
                check_car_s += ((double) prev_size * 0.02 * check_speed);

                if(check_car_s > car_s)
                  // ahead
                {
                  if(check_car_s - car_s < second_ahead_s)
                  {
                    second_ahead_s = check_car_s - car_s;
                  }
                } else
                  // behind
                {
                  if(check_car_s - car_s > second_behind_s)
                  {
                    second_behind_s = check_car_s - car_s;
                  }
                }
              } else if(d < (2+4*2+2) && d > (2+4*2-2))
              {
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx * vx + vy * vy);
                double check_car_s = sensor_fusion[i][5];
                check_car_s += ((double) prev_size * 0.02 * check_speed);

                if(check_car_s > car_s)
                  // ahead
                {
                  if(check_car_s - car_s < third_ahead_s)
                  {
                    third_ahead_s = check_car_s - car_s;
                  }
                } else
                  // behind
                {
                  if(check_car_s - car_s > third_behind_s)
                  {
                    third_behind_s = check_car_s - car_s;
                  }
                }
              }


//              if (lane > 0)
//              // think about left
//              {
//                int left_lane = lane - 1;
//                left_behind_s = - 100;
//                left_ahead_s = 100;
//                if(d < (2+4*left_lane+2) && d > (2+4*left_lane-2))
//                {
//                  double vx = sensor_fusion[i][3];
//                  double vy = sensor_fusion[i][4];
//                  double check_speed = sqrt(vx * vx + vy * vy);
//                  double check_car_s = sensor_fusion[i][5];
//
//                  check_car_s += ((double) prev_size * 0.02 * check_speed);
//                  if (check_car_s > car_s)
//                  // ahead
//                  {
//                    if (check_car_s - car_s < left_ahead_s)
//                    {
//                      left_ahead_s = check_car_s;
//                    }
//                  }
//                  else{
//                  // behind
//                    if (check_car_s - car_s > left_behind_s)
//                    {
//                      left_behind_s = check_car_s;
//                    }
//                  }
//                }
//                if ((left_ahead_s > safe_ahead) && (left_behind_s < safe_behind))
//                // safe to merge left
//                {
//                  turn_left = true;
//                }
//
//              }
//              if (lane < 3)
//              // think about right
//              {
//                int right_lane = lane + 1;
//                right_behind_s = - 100;
//                right_ahead_s = 100;
//                if(d < (2+4*right_lane+2) && d > (2+4*right_lane-2))
//                {
//                  double vx = sensor_fusion[i][3];
//                  double vy = sensor_fusion[i][4];
//                  double check_speed = sqrt(vx * vx + vy * vy);
//                  double check_car_s = sensor_fusion[i][5];
//
//                  check_car_s += ((double) prev_size * 0.02 * check_speed);
//                  if (check_car_s > car_s)
//                    // ahead
//                  {
//                    if (check_car_s - car_s < right_ahead_s)
//                    {
//                      right_ahead_s = check_car_s;
//                    }
//                  }
//                  else{
//                    // behind
//                    if (check_car_s - car_s > right_behind_s)
//                    {
//                      right_behind_s = check_car_s;
//                    }
//                  }
//                }
//                if ((right_ahead_s > safe_ahead) && (right_behind_s < safe_behind))
//                  // safe to merge left
//                {
//                  turn_right = true;
//                }
//              }
            }

            // find goal
            int goal;
            if((first_ahead_s >= second_ahead_s) && (first_ahead_s >= third_ahead_s))
            {
              goal = 0;
            }
            else if((second_ahead_s >= first_ahead_s) && (second_ahead_s >= third_ahead_s))
            {
              goal = 1;
            }
            else if((third_ahead_s >= first_ahead_s) && (third_ahead_s >= second_ahead_s))
            {
              goal = 2;
            }

//            cout<<"lane: "<<lane<<endl;
//            cout<<"goal: "<< goal << endl;
//            cout<<"first lane:"<<first_ahead_s<<" "<<first_behind_s<<endl;
//            cout<<"second lane:"<<second_ahead_s<<" "<<second_behind_s<<endl;
//            cout<<"third lane:"<<third_ahead_s<<" "<<third_behind_s<<endl;

            // merge to goal lane
            if(lane == 0)
            {
              if((goal == 1 || goal == 2) && second_ahead_s >= safe_ahead && second_behind_s <= safe_behind)
              {
                lane = 1;
              }
//              else if(goal == 2 && second_ahead_s >= safe_ahead && second_behind_s <= safe_behind && third_ahead_s >=
//                  safe_ahead && third_behind_s <= safe_behind)
//              {
//                lane = 2;
//              }
            }
            else if (lane == 1)
            {
//              cout<<first_ahead_s<<" "<<first_behind_s<<" "<< endl;
//              cout<<third_ahead_s<<" "<<third_behind_s<<" "<< endl;
              if(goal == 0 && first_ahead_s >= safe_ahead && first_behind_s <= safe_behind)
              {
                lane = 0;
              }
              else if(goal == 2 && third_ahead_s >= safe_ahead && third_behind_s <= safe_behind)
              {
                lane = 2;
              }
            }
            else if (lane == 2)
            {
//              bool first = (first_ahead_s >= safe_ahead && first_behind_s <= safe_behind);
//              bool second = (second_ahead_s >= safe_ahead && second_behind_s <= safe_behind);
//              cout<<first<< " "<<second<<endl;
//              if(goal == 0 && first_ahead_s >= safe_ahead && first_behind_s <= safe_behind && second_ahead_s >=
//                  safe_ahead && second_behind_s <= safe_behind)
              if((goal == 0 || goal == 1) && second_ahead_s >= safe_ahead && second_behind_s <= safe_behind)
              {
                lane = 1;
              }
//              else if (goal == 1 && second_ahead_s >= safe_ahead && second_behind_s <= safe_behind)
//              {
//                lane = 1;
//              }
            }
//            cout<<"lane: "<<lane<<endl;
          }
          else if (ref_vel < 49.5)
          {
            ref_vel += 0.224 * 1.5;
          }

					// create a list of widely spaced (x, y) waypoints, evenly spaced at 30m
					vector<double> ptsx;
					vector<double> ptsy;

					//reference x, y, yaw states
					double ref_x = car_x;
					double ref_y = car_y;
					double ref_yaw = deg2rad(car_yaw);

					// if previous size is almost empty, use the car as starting reference
					if(prev_size < 2)
					{
						// use two points that make the path tangent to the car
						double prev_car_x = car_x - cos(car_yaw);
						double prev_car_y = car_y - sin(car_yaw);

						ptsx.push_back(prev_car_x);
						ptsx.push_back(car_x);

						ptsy.push_back(prev_car_y);
						ptsy.push_back(car_y);
					}
					// use the previous path's end point as starting reference
					else
					{
						//redefine reference state as previous path end point
						ref_x = previous_path_x[prev_size - 1];
						ref_y = previous_path_y[prev_size - 1];

						double ref_x_prev = previous_path_x[prev_size - 2];
						double ref_y_prev = previous_path_y[prev_size - 2];
						ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

						//use two points that make the path tangent to the previous path's end point
						ptsx.push_back(ref_x_prev);
						ptsx.push_back(ref_x);

						ptsy.push_back(ref_y_prev);
						ptsy.push_back(ref_y);
					}

          int first_s;
          int second_s;
          int third_s;

          if((old_lane - 1 == lane) || (old_lane + 1 == lane))
          //In Frenet add evenly 30m spaced points ahead of the starting reference
          {
            first_s = 40;
            second_s = 70;
            third_s = 90;
          }
          else if ((old_lane - 2 == lane) || (old_lane + 2 == lane))
          {
            first_s = 50;
            second_s = 70;
            third_s = 90;
          }
          else
          {
            first_s = 30;
            second_s = 60;
            third_s = 90;
          }
//          first_s = 30;
//          second_s = 60;
//          third_s = 90;

					vector<double> next_wp0 = getXY(car_s + first_s, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
					vector<double> next_wp1 = getXY(car_s + second_s, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
					vector<double> next_wp2 = getXY(car_s + third_s, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

					ptsx.push_back(next_wp0[0]);
					ptsx.push_back(next_wp1[0]);
					ptsx.push_back(next_wp2[0]);

					ptsy.push_back(next_wp0[1]);
					ptsy.push_back(next_wp1[1]);
					ptsy.push_back(next_wp2[1]);

//          cout << "============================================" << endl;
//          for (int i=0; i<ptsx.size(); i++)
//          {
//            cout << "point " << i << ": " << "x = " << ptsx[i] << ", y = " << ptsy[i] << ", car_s: " << car_s << endl;
//          }

					// transformation to local coordinate system
					for(int i=0; i<ptsx.size(); i++)
					{
						//shift car reference angle to 0 degrees
						double shift_x = ptsx[i] - ref_x;
						double shift_y = ptsy[i] - ref_y;

						ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
						ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));
					}

					// create a spline
					tk::spline s;

					// set (x,y) points to the spline
//          for (int i=0; i<ptsx.size(); i++)
//          {
//            cout << "point " << i << ": " << "x = " << ptsx[i] << ", y = " << ptsy[i] << ", car_s: " << car_s << endl;
//          }
					s.set_points(ptsx, ptsy);

					// define the actual (x, y) points we will use for the planner
					vector<double> next_x_vals;
					vector<double> next_y_vals;

					// start with all of the previous path points from last time
					for(int i=0; i<previous_path_x.size(); i++)
					{
						next_x_vals.push_back(previous_path_x[i]);
						next_y_vals.push_back(previous_path_y[i]);
					}

					// calculate how to break up spline points so that we travel at tour designed reference velocity
					double target_x = 30.0;
					double target_y = s(target_x);
					double target_dist = sqrt((target_x) * (target_x) + (target_y) * (target_y));

					double x_add_on = 0;

					// Fill up the rest of the path planner after filling it with previous
					for (int i=1; i<=50-previous_path_x.size(); i++)
					{
						double N = (target_dist / (0.02 * ref_vel / 2.24));
						double x_point = x_add_on + (target_x) / N;
						double y_point = s(x_point);

						x_add_on = x_point;

						double x_ref = x_point;
						double y_ref = y_point;

						// rotate back to normal after rotating it earlier
						x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
						y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));

						x_point += ref_x;
						y_point += ref_y;

						next_x_vals.push_back(x_point);
						next_y_vals.push_back(y_point);
					}
//          cout << next_x_vals.size() << " " << next_y_vals.size() << endl;


					json msgJson;


//					double dist_inc = 0.5;
//					for(int i=0; i<50; i++)
//					{
//							double next_s = car_s + (i+1) * dist_inc;
//							double next_d = 6;
//							vector<double> xy = getXY(next_s, next_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);
//
//							next_x_vals.push_back(xy[0]);
//							next_y_vals.push_back(xy[1]);
//					}

					// END
					msgJson["next_x"] = next_x_vals;
					msgJson["next_y"] = next_y_vals;

					auto msg = "42[\"control\","+ msgJson.dump()+"]";

					//this_thread::sleep_for(chrono::milliseconds(1000));
					ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
