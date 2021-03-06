#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/Twist.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_polygonal_prism_data.h>
#include <pcl/surface/concave_hull.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/statistical_outlier_removal.h>

#include <Eigen/Dense>

#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#define PI 3.141592653589
using namespace std;
ros::Subscriber livox_lidar_sub;
ros::Publisher  height_pc_pub ,filtered_pc_pub ,rotated_pc_pub, plane_pc_pub, target_pub;
pcl::PointXYZ highest_point;
pcl::PointXYZ current_target;

double max_distance_multi = 0;
int total_frame = 0;

//params
string lidar_topic;
double min_distance , min_lidar_distance ;
int frame_tick;
int neighbor_points;
double threshold;
double max_target_height;
double d_width;
double target_lose_distance;
double vector_length;
double splash_size = 30;
bool detect_mode;
bool debug_mode;

vector<double> tem_x;
vector<double> tem_y;

class TargetFound {

public:
    Eigen::Vector3f centroid_coor;
    Eigen::Vector3f current_point;
    int freq = 0;
    int time_size = 0;
public:
    TargetFound(pcl::PointXYZ coor) {
        centroid_coor << coor.x , coor.y , coor.z;
        current_point << coor.x , coor.y , coor.z;
    }
    void addPoint(Eigen::Vector3f new_point) {
        centroid_coor = ( freq * centroid_coor + new_point ) / (freq + 1);
        freq++;
        if (freq > splash_size) {freq = splash_size;}
        current_point = new_point;
    }
    void timeAttenuation(){
        time_size ++;
        if(time_size % 2 == 0) {
            freq --;
            if(freq < 0){freq = 0;}
        }
    }
};

vector<TargetFound> targets_manager;

double eu_distance(double x , double y , double z) {
    return sqrt(x*x + y*y + z*z);
}

void updateTargets(pcl::PointXYZ &coor){

    int index = 0;
    double min_distance = 10000;
    bool is_new_target = true;
    for(int i = 0 ; i < targets_manager.size() ; i++) {
        double distance = eu_distance( targets_manager[i].centroid_coor[0] - coor.x ,
                                       targets_manager[i].centroid_coor[1] - coor.y ,
                                       targets_manager[i].centroid_coor[2] - coor.z );
        if (distance < min_distance && distance < target_lose_distance) {
            index = i;
            min_distance = distance;
            is_new_target = false;
        }
        targets_manager[i].timeAttenuation();
    }
    if (is_new_target){
        targets_manager.push_back(TargetFound(coor));
        return ;
    }
    else {
        targets_manager[index].addPoint(Eigen::Vector3f(coor.x , coor.y , coor.z));
        return ;
    }

}

void publishTarget() {
    
    int index = 0;
    int max_freq = -10000;
    geometry_msgs::Twist target;
    for(int i = 0 ; i < targets_manager.size() ; i++) {

        if (max_freq < targets_manager[i].freq) {
            index = i;
            max_freq = targets_manager[i].freq;
        }

        if (targets_manager[i].freq > 0){
            cout <<" f: "<<targets_manager[i].freq << "  ";
        }
    }

    sensor_msgs::PointCloud2 output_pc ;
    pcl::PointCloud<pcl::PointXYZ>::Ptr output_cloud (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointXYZ tgt;
    tgt.x = targets_manager[index].current_point[0];
    tgt.y = targets_manager[index].current_point[1];
    tgt.z = targets_manager[index].current_point[2];

    output_cloud -> push_back(tgt);

    pcl::toROSMsg(*output_cloud , output_pc);
    output_pc.header.frame_id = "livox_frame";
    output_pc.header.stamp    = ros::Time::now();
    height_pc_pub.publish(output_pc);
    
    target.linear.x = -tgt.x;
    target.linear.y = -tgt.y;
    target.linear.z = -tgt.z;
    target.angular.x = 1;

    target_pub.publish(target);

    cout<<"\npublished!  dx="<< -tgt.x<<" | dy="<< -tgt.y <<" | dz=" << -tgt.z <<endl;

}

void initParams(ros::NodeHandle& n)
{

    if(!n.getParam("/highest_pc_node/check_cloud", lidar_topic)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/min_distance", min_distance)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/min_lidar_distance", min_lidar_distance)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/frame_tick", frame_tick)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/filter_neighbor_points", neighbor_points)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/filter_threshold", threshold)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/max_target_height", max_target_height)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/d_width", d_width)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/target_lose_distance", target_lose_distance)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/vector_length", vector_length)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/splash_size", splash_size)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/debug_mode", debug_mode)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/detect_mode", detect_mode)){ ROS_ERROR("Failed to get parameter from server."); }

    cout << "load params successfully as: \n";
    cout << "check_cloud : " << lidar_topic << "\n\n min_distance : " << min_distance << "\n\n min_lidar_distance : " << min_lidar_distance << endl;
    cout << "frame_tick : " << frame_tick << "\nfilter_neighbor_points : " << neighbor_points << "\nfilter_threshold : " << threshold <<endl;
    cout << "d_width : " << d_width << "\n vector_length : " << vector_length <<"\ntarget_lose_distance : "<< target_lose_distance <<endl;
    cout << "splash_size : " <<splash_size << endl;
    cout << "max_target_height : " << max_target_height << "\n\n" << "debug_mode : " << debug_mode << "\ndetect_mode: " << detect_mode << endl;
    
    ROS_INFO("\n====highest point detect node init====\n");
}

// not used
double getPitchFromCloud(pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud) {

    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
    pcl::SACSegmentation<pcl::PointXYZ> seg;
    seg.setOptimizeCoefficients (true);
    seg.setModelType (pcl::SACMODEL_PLANE);
    seg.setMethodType (pcl::SAC_RANSAC);
    seg.setDistanceThreshold (0.01);
    seg.setMaxIterations(500);
    seg.setInputCloud (point_cloud);
    seg.segment (*inliers, *coefficients);
    double theta = acos(fabs(coefficients -> values[2]));
    return theta;

}


void pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pc_now) {

    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);

    pcl::PointCloud<pcl::PointXYZ>::Ptr pc_raw (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_plane (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg( *pc_now, *pc_raw );


    double maxdistance = 0 , index = 0;
    double distance = 0;
    geometry_msgs::Twist target;

    pcl::SACSegmentation<pcl::PointXYZ> seg;
    seg.setOptimizeCoefficients (true);
    seg.setModelType (pcl::SACMODEL_PLANE);
    seg.setMethodType (pcl::SAC_RANSAC);
    seg.setDistanceThreshold (0.01);
    seg.setMaxIterations(500);
    seg.setInputCloud (pc_raw);
    seg.segment (*inliers, *coefficients);

    // ?????????????????????????????????????????????
    if(debug_mode) {
        pcl::ExtractIndices<pcl::PointXYZ> ex ;
        ex.setInputCloud(pc_raw);
        ex.setIndices(inliers);
        ex.filter(*cloud_plane); 
    }

    //????????????
    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;       //?????????????????????
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZ>);  
    sor.setInputCloud (pc_raw);                              //????????????????????????
    sor.setMeanK (neighbor_points);                          //???????????????????????????????????????????????????
    sor.setStddevMulThresh (threshold);                      //???????????????????????????????????????
    sor.filter (*cloud_filtered);                            //??????

    float A = coefficients -> values[0];
    float B = coefficients -> values[1];
    float C = coefficients -> values[2];
    float D = coefficients -> values[3];
    double d = sqrt(A*A + B*B + C*C);
    double theta = asin(fabs(A));
    double roll  = asin(B);

    if (C > 0) {theta = PI - theta;}
    cout<<"theta = "<< (theta * 180 / PI) << "  roll = " << (roll * 180 / PI)  << endl;
    for(int i = 0 ; i < cloud_filtered -> points.size() ; i++) {

        if(  eu_distance(cloud_filtered -> points[i].x,    cloud_filtered -> points[i].y,    cloud_filtered -> points[i].z) < min_lidar_distance){continue;}
        distance = abs(A*cloud_filtered -> points[i].x + B*cloud_filtered -> points[i].y + C*cloud_filtered -> points[i].z + D) / d;
        if (distance > max_target_height) {continue;}
        if (distance > maxdistance) { maxdistance = distance ; index = i;}
    }

    // transform the cloud
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_rotated (new pcl::PointCloud<pcl::PointXYZ>);
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
                                                             // z-y-x
    Eigen::AngleAxisf Rotation_vector_y(theta , Eigen::Vector3f(0,1,0));
    Eigen::AngleAxisf Rotation_vector_x(-roll , Eigen::Vector3f(0,0,1));
    Eigen::Matrix3f rotation_matrix = Rotation_vector_y.matrix() * Rotation_vector_x.matrix();
    transform.block<3,3>(0,0) = rotation_matrix;


    pcl::transformPointCloud(*cloud_filtered , *cloud_rotated , transform);



    if (debug_mode) {
        sensor_msgs::PointCloud2 output_pc_rotated, output_pc_filtered ,output_pc_plane;
        pcl::toROSMsg(*cloud_rotated , output_pc_rotated);
        pcl::toROSMsg(*cloud_plane , output_pc_plane);
        pcl::toROSMsg(*cloud_filtered , output_pc_filtered);
        output_pc_rotated.header.frame_id  = "livox_frame";
        output_pc_plane.header.frame_id    = "livox_frame";
        output_pc_filtered.header.frame_id = "livox_frame";
        output_pc_rotated.header.stamp     = ros::Time::now();
        output_pc_plane.header.stamp     = ros::Time::now();
        output_pc_filtered.header.stamp     = ros::Time::now();
        filtered_pc_pub.publish(output_pc_filtered);
        plane_pc_pub.publish(output_pc_plane);
        rotated_pc_pub.publish(output_pc_rotated);
    }

    // point not found
    if (fabs(maxdistance) < min_distance) {
        
        target.angular.x = 0;
        target.angular.z = -D/d;
        target_pub.publish(target);
        return ;
    
    }
    
    pcl::PointXYZ last_highest_point = highest_point;
    if ( maxdistance > max_distance_multi - 0.1 || total_frame > frame_tick){
        total_frame = 0;
        max_distance_multi = maxdistance;
        highest_point = cloud_filtered -> points[index];
    }

    
    total_frame++ ;
    //drone coordinate
        
    Eigen::Vector4f hp_coor, hp_coor_last ,hp_coor_current_target;
    hp_coor      << highest_point.x , highest_point.y , highest_point.z , 1;
    hp_coor_last << last_highest_point.x , last_highest_point.y , last_highest_point.z , 1;
    Eigen::Vector4f hp_coor_d      = transform * hp_coor;
    Eigen::Vector4f hp_coor_d_last = transform * hp_coor_last;
    pcl::PointXYZ p_drone;


    //???????????????
    if (fabs(hp_coor_d_last[0] - hp_coor_d[0]) < d_width && fabs(hp_coor_d_last[1] - hp_coor_d[1]) < d_width) { 
        
        tem_x.push_back(hp_coor_d[0]);
        tem_y.push_back(hp_coor_d[1]);
        if (tem_x.size() > vector_length) {tem_x.erase(tem_x.begin());}
        if (tem_y.size() > vector_length) {tem_y.erase(tem_y.begin());}


        cout <<" smooth_filter.length = " << tem_x.size() <<endl;
        double sum_x = accumulate(begin(tem_x), end(tem_x), 0.0);  
        double mean_x =  sum_x / tem_x.size(); //??????
        double sum_y = accumulate(begin(tem_y), end(tem_y), 0.0);  
        double mean_y =  sum_y / tem_y.size(); //??????
        p_drone.x = mean_x;
        p_drone.y = mean_y;
    }

    else {
        tem_x.clear();
        tem_y.clear();
        p_drone.x = hp_coor_d[0];
        p_drone.y = hp_coor_d[1];
    }

    p_drone.z = hp_coor_d[2];

    updateTargets(p_drone);
    publishTarget();

}

//MAIN
int main(int argc, char** argv)
{
    ros::init(argc, argv, "highest_exactor_node");
    ros::NodeHandle nh;

    initParams(nh);

    livox_lidar_sub =
        nh.subscribe(lidar_topic , 1000, pointCloudCallback);

    height_pc_pub =
        nh.advertise<sensor_msgs::PointCloud2>("/highest_point_cloud", 1000);

    filtered_pc_pub =
        nh.advertise<sensor_msgs::PointCloud2>("/filtered_point_cloud", 1000); 
    
    rotated_pc_pub = 
        nh.advertise<sensor_msgs::PointCloud2>("/rotated_point_cloud", 1000); 

    plane_pc_pub = 
        nh.advertise<sensor_msgs::PointCloud2>("/plane_point_cloud", 1000); 
    
    target_pub = 
        nh.advertise<geometry_msgs::Twist>("/target", 1000); 


    ros::spin();
    return 0;
}

