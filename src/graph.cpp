#include "ros/ros.h"
#include "graph.h"

using namespace std;

const int N_INF = -9999999;
const int P_INF = 9999999;
const double PI = 3.141592654;

void Graph::Graph(double resolution, double range_threshold) {
	this->resolution = resolution;
	this->range_threshold = range_threshold;
};

void Graph::addNode(geometry_msgs::Pose pose, sensor_msgs::LaserScan scan){
	Node n;
	n.pose = pose;
	n.scan = scan;
	n.scan_grid = scanToOccGrid(scan, pose);
	node_list.push_back(n);
	// TODO: Match the new node's scans to previous scans and add edges accordingly
	Edge e;
	e.parent = last_node;
	e.child = n;
	edge_list.push_back(e);
    last_node = n;
}
;

// Combine the scan-grids in the nodes into a complete map!
void Graph::generateMap() {
    // First we need to know the outer-bounds of the map
    double xmax = N_INF,xmin = P_INF,ymax = N_INF,ymin = P_INF;
    for (unsigned int i = 0; i < node_list.size(); i++)
    {
        Node* node = nodes[i];
        if (xmax < (node->x + (node->scan_grid.xmax)) * resolution)
            xmax = (node->x + (node->scan_grid.xmax)) * resolution;

        if (xmin > (node->x - (node->scan_grid.xmin)) * resolution)
            xmin = (node->x - (node->scan_grid.xmin)) * resolution;

        if (ymax < (node->y + (node->scan_grid.ymax)) * resolution)
            ymax = (node->y + (node->scan_grid.ymax)) * resolution;

        if (ymin > (node->y - (node->scan_grid.ymin)) * resolution)
            ymin = (node->y - (node->scan_grid.ymin)) * resolution;
    }
    // Map size
    double map_height = (ymax - ymin) / resolution;
    double map_width = (xmax - xmin) / resolution;
    double map_size = map_height * map_width;
    //
    cur_map.header.frame_id = "/odom";
    cur_map.header.stamp = ros::Time()::now();
    cur_map.info.height = map_height;
    cur_map.info.width = map_width;
    cur_map.info.resolution = resolution;
    //
    cur_map.data.resize(map_size);
    // This vector counts how many times a map-position was seen in the graph
    // Later we use this to compute the certainty that a position is an obstacle
    vector<int> pos_seen (map_size, 0);
    // These will count how many times a position was blocked/free. All others will be unknown
    vector<int> pos_free (map_size, 0);
    vector<int> pos_blocked (map_size, 0);
    // Go through all nodes' scan-grids and maintain the position information for each
    for (unsigned int i = 0; i < node_list.size; i++)
    {
        Node* node = node_list[i];
        int node_x = round((node->x-xmin)/resolution)-node->scanMap.left;
        int node_y = round((node->y-ymin)/resolution)-node->scanMap.down;
        // Go through the local map, and count the occupancies for the global map
        for (unsigned int j = 0; j < node->scan_grid.height; j++) {
            for (unsigned int k = 0; k < node->scan_grid.width; k++) {
                // Index in the global map
                int global_index = (node_y + j) * map_width + node_x + k;
                pos_seen[global_index]++;
                // The value of the local grid
                int local_index = j * node->scan_grid.width + k;
                int value = node->scan_grid.grid[local_index];
                //
                if (value == 100) // Position is blocked
                    pos_blocked[global_index]++;
                if (value == 0) // Position is unoccupied
                    pos_free[global_index]++;

            }
        }
    }
    // Now, we can update the global map according to what we saw in the local maps
    for(unsigned int i = 0; i < map_size; i++) {
        // The position was free more often than blocked
        if(pos_free[i] > pos_blocked[i])
            cur_map.data[i] = 0;
        else if (pos_free[i] < pos_blocked[i])
            cur_map.data[i] = 100;
        else if (pos_free[i] == pos_blocked[i]) {
            // The position was as often blocked as free, use the seen to break the tie
            // TODO: What to do here? Set to unknown so it gets visited again?
            cur_map.data[i] = -1;
        } else {
            cur_map.data[i] = -1;
        }
    }
}
;

// Take a scan as input, generate a small, local occupancy grid
ScanGrid Graph::scanToOccGrid(sensor_msgs::LaserScan& scan, geometry_msgs::Pose& pose){
	ScanGrid new_grid;
	double angle_incr = scan.angle_increment;
	// First we need to know the size and bounds of the grid.
    double xmax = N_INF,xmin = P_INF,ymax = N_INF,ymin = P_INF;
    double scan_angle, scan_x, scan_y, min_angle = scan.angle_min;
    int num_scans = scan.ranges.size();
    double pose_theta = tf::getYaw(pose);
    // Get the scan positions the bound the grid
    for (int i = 0; i < num_scans; i++)
    {
        scan_angle = pose_theta + min_angle + i * angle_incr;
        if (ymax < pose.y + scan.ranges[i] * sin(scan_angle))
            ymax = pose.y + scan.ranges[i] * sin(scan_angle);
        if (xmax < pose.x + scan.ranges[i] * cos(scan_angle))
            xmax = pose.x + scan.ranges[i] * cos(scan_angle);
        if (ymin > pose.y + scan.ranges[i] * sin(scan_angle))
            ymin = pose.y + scan.ranges[i] * sin(scan_angle);
        if (xmin > pose.x + scan.ranges[i] * cos(scan_angle))
            xmin = pose.x + scan.ranges[i] * cos(scan_angle);
    }
    // Initialize the grid, set the bounds relative to the odometry
    new_grid.ymax = round((ymax - pose.y) / resolution);
    new_grid.ymin = round((pose.y - ymin) / resolution);
    new_grid.xmax = round((xmax - pose.x) / resolution);
    new_grid.xmin = round((pose.x - xmin) / resolution);
    // Size of the grid can be computed with the bounds
    new_grid.height = new_grid.ymax + new_grid.ymin;
    new_grid.width = new_grid.xmin + new_grid.xmax;
    // Set the grid to the correct size
    new_grid.resolution = this->resolution;
    int grid_size = new_grid.height * new_grid.width
    new_grid.grid.resize(grid_size);
    // Set each cell to unknown space, later we will it with obstacles and known space
    for (int i= 0; i < grid_size; i++) {
        new_grid.grid[i] = -1;
    }
    //
   	double max_range = scan.range_max * range_threshold, measurement;
   	double theta, x, y;
   	int row, col;
    for (int i = 0; i < num_scans; i++)
    {
        measurement = node->scan.ranges[i];
        // Check if out of range, dont place an obstacle on the grid in this case
        if (measurement > max_range)
            continue;
        // Determine and set the location of the object in the local grid
        theta = pose_theta + min_angle + i * angle_incr;
        x = measurement * cos(theta);
        y = measurement * sin(theta);
        col = new_grid.ymin + round(y / resolution);
        row = new_grid.xmin + round(x / resolution);
        // 
        new_grid.grid[col * new_grid.width + row] = 100;
    }
    // Use triangulation to fill the grid covered by the scan with known space
    double range, scan_dist, scan_theta, scan_end;
    int index, scan_index;
    for (unsigned int i = 0; i < new_grid.height; i++) {
        for (unsigned int j = 0; j < new_grid.width; j++) {
            index = i * new_grid.width + j;
            // We can skip positions we know are occupied
            if (new_grid.grid[index] == 100)
                continue;
            scan_theta = atan2(i - new_grid.ymin, j - new_grid.xmin) - pose_theta - scan.angle_min;
            scan_theta -= floor(scan_theta / PI / 2) * (PI * 2);
            // Check if the scan is out of bounds
            scan_index = round(scan_theta / scan.angle_increment);
            if (scan_index < 0|| scan_index >= num_scans) {
                new_grid.grid[index] = -1;
                continue;
            }
            //
            range = scan.ranges[scan_index];
            scan_dist = sqrt(pow(j - new_grid.xmin, 2) + pow(i - new_grid.ymin, 2));
            scan_end = range - (scan_dist * resolution);
            // If the end of the scan is outside the bounds of the grid, it will be unknown
            // For instance, it will be behind a wall. Otherwise, it is in between the robot and the wall.
            if(scan_end > 0)
                new_grid.grid[index] = 0;
            else
                new_grid.grid[index] = -1;
        }
    }
    return new_grid;
};
