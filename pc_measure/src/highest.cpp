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
ros::Publisher  height_pc_pub ,filtered_pc_pub ,rotated_pc_pub, target_pub;
pcl::PointXYZ highest_point;

double max_distance_multi = 0;
int total_frame = 0;

//params
string lidar_topic;
double min_distance , min_lidar_distance ;
int frame_tick;
int neighbor_points;
double threshold;
bool debug_mode;

vector<double> tem_x(10);
vector<double> tem_y(10);


void initParams(ros::NodeHandle& n)
{

    if(!n.getParam("/highest_pc_node/check_cloud", lidar_topic)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/min_distance", min_distance)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/min_lidar_distance", min_lidar_distance)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/frame_tick", frame_tick)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/filter_neighbor_points", neighbor_points)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/filter_threshold", threshold)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/filter_threshold", threshold)){ ROS_ERROR("Failed to get parameter from server."); }
    if(!n.getParam("/highest_pc_node/debug_mode", debug_mode)){ ROS_ERROR("Failed to get parameter from server."); }

    cout << "load params successfully as: \n";
    cout << "check_cloud : " << lidar_topic << "\n\n min_distance : " << min_distance << "\n\n min_lidar_distance : " << min_lidar_distance << endl;
    cout << "frame_tick : " << frame_tick << "filter_neighbor_points : " << neighbor_points << "filter_threshold : " << threshold <<endl;
    
    ROS_INFO("\n====highest point detect node init====\n");
}

double eu_distance(double x , double y , double z) {
    return sqrt(x*x + y*y + z*z);
}

void pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pc_now) {

    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);

    pcl::PointCloud<pcl::PointXYZ>::Ptr pc_raw (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg( *pc_now, *pc_raw );
    geometry_msgs::Twist target;

    /*
    Eigen::Matrix4f transform_pre = Eigen::Matrix4f::Identity();
    Eigen::AngleAxisf Rotation_vector_pre(PI/2, Eigen::Vector3f(0,1,0));
    Eigen::Matrix3f rotation_matrix_pre = Rotation_vector_pre.matrix();
    transform_pre.block<3,3>(0,0) = rotation_matrix_pre;
    pcl::transformPointCloud(*pc_raw_pre , *pc_raw , transform_pre);*/

    pcl::SACSegmentation<pcl::PointXYZ> seg;
    seg.setOptimizeCoefficients (true);
    seg.setModelType (pcl::SACMODEL_PLANE);
    seg.setMethodType (pcl::SAC_RANSAC);
    seg.setDistanceThreshold (0.01);
    seg.setMaxIterations(500);
    seg.setInputCloud (pc_raw);
    seg.segment (*inliers, *coefficients);

    // 把平面内点提取到一个新的点云中
    if(debug_mode) {
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_plane (new pcl::PointCloud<pcl::PointXYZ>);
        pcl::ExtractIndices<pcl::PointXYZ> ex ;
        ex.setInputCloud(pc_raw);
        ex.setIndices(inliers);
        ex.filter(*cloud_plane); 
    }

    //统计滤波
    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;   //创建滤波器对象
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZ>);
    sor.setInputCloud (pc_raw);                           //设置待滤波的点云
    sor.setMeanK (neighbor_points);                               //设置在进行统计时考虑查询点临近点数
    sor.setStddevMulThresh (threshold);                      //设置判断是否为离群点的阀值
    sor.filter (*cloud_filtered);                    //存储

    float A = coefficients -> values[0];
    float B = coefficients -> values[1];
    float C = coefficients -> values[2];
    float D = coefficients -> values[3];
    double d = sqrt(A*A + B*B + C*C);
    double theta = acos(fabs(C));
    double maxdistance = 0 , index = 0;
    double distance = 0;

    theta = PI - theta;
    //cout<<"theta = "<< (theta * 180 / PI)<<endl;
    for(int i = 0 ; i < cloud_filtered -> points.size() ; i++) {

        if(  eu_distance(cloud_filtered -> points[i].x,    cloud_filtered -> points[i].y,    cloud_filtered -> points[i].z) < min_lidar_distance){continue;}
        distance = abs(A*cloud_filtered -> points[i].x + B*cloud_filtered -> points[i].y + C*cloud_filtered -> points[i].z + D) / d;
        if (distance > maxdistance) { maxdistance = distance ; index = i;}
    }

    // transform the cloud
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_rotated (new pcl::PointCloud<pcl::PointXYZ>);
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    Eigen::AngleAxisf Rotation_vector(theta , Eigen::Vector3f(0,1,0));
    Eigen::Matrix3f rotation_matrix = Rotation_vector.matrix();
    transform.block<3,3>(0,0) = rotation_matrix;

    if (debug_mode) {
        pcl::transformPointCloud(*cloud_filtered , *cloud_rotated , transform);
        sensor_msgs::PointCloud2 output_pc_rotated, output_pc_filtered;
        pcl::toROSMsg(*cloud_rotated , output_pc_rotated);
        pcl::toROSMsg(*cloud_filtered , output_pc_filtered);
        output_pc_rotated.header.frame_id = "livox_frame";
        output_pc_filtered.header.frame_id = "livox_frame";
        filtered_pc_pub.publish(output_pc_filtered);
        rotated_pc_pub.publish(output_pc_rotated);
    }

    // point not found
    if (fabs(maxdistance) < min_distance) {
        
        target.angular.x = 0;
        target_pub.publish(target);
        return ;
    
    }
    if ( maxdistance > max_distance_multi - 0.2 || total_frame > frame_tick){
        total_frame = 0;
        max_distance_multi = maxdistance;
        pcl::PointXYZ last_highest_point = highest_point;
        highest_point = cloud_filtered -> points[index];

        //过滤钢轨差异
        if (fabs(last_highest_point.x - highest_point.x) < 0.3 && fabs(last_highest_point.y - highest_point.y) < 0.3) { 

            tem_x.push_back(highest_point.x);
            tem_y.push_back(highest_point.y);
            double sum_x = accumulate(begin(tem_x), end(tem_x), 0.0);  
            double mean_x =  sum_x / tem_x.size(); //均值
            double sum_y = accumulate(begin(tem_y), end(tem_y), 0.0);  
            double mean_y =  sum_y / tem_y.size(); //均值
            highest_point.x = mean_x;
            highest_point.y = mean_y;
        }
        else{
            tem_x.clear();
            tem_y.clear();
        }
    }

    sensor_msgs::PointCloud2 output_pc ;
    pcl::PointCloud<pcl::PointXYZ>::Ptr output_cloud (new pcl::PointCloud<pcl::PointXYZ>);
    output_cloud -> push_back(highest_point);
    total_frame++ ;
    //drone coordinate
        
    Eigen::Vector4f hp_coor;
    hp_coor << highest_point.x , highest_point.y , highest_point.z , 1;
    Eigen::Vector4f hp_coor_d = transform * hp_coor;
    pcl::PointXYZ p_drone;
    p_drone.x = hp_coor_d[0];
    p_drone.y = hp_coor_d[1];
    p_drone.z = hp_coor_d[2];
    output_cloud -> push_back(p_drone);

    pcl::toROSMsg(*output_cloud , output_pc);
    output_pc.header.frame_id = "livox_frame";

    height_pc_pub.publish(output_pc);

    target.linear.x = -p_drone.x;
    target.linear.y = -p_drone.y;
    target.linear.z = -p_drone.z;
    target.angular.x = 1;

    target_pub.publish(target);

    cout<<"\npublished!  dx="<< -p_drone.x <<" | dy="<< -p_drone.y <<" | dz=" << -p_drone.z <<endl;

}

//驴肉火烧
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
    
    target_pub = 
        nh.advertise<geometry_msgs::Twist>("/target", 1000); 


    ros::spin();
    return 0;
}
