//
// Created by atakan on 08.06.2023.
//

#ifndef CPP_PUBSUB_MAZESOLVER_H
#define CPP_PUBSUB_MAZESOLVER_H

#include "rclcpp/rclcpp.hpp" // ROS C++ API
#include "std_msgs/msg/float64_multi_array.hpp" // ROS message type
#include <iostream> // Input/output stream
#include <vector> // Standard vector container
#include <cmath> // Math functions
#include <algorithm> // Algorithms library
#include "cpp_pubsub/astar.h" // Custom A* algorithm implementation
#include <fstream> // File handling
// Define operator<< for printing vectors
template <typename S>
std::ostream& operator<<(std::ostream& os, const std::vector<S>& vector)
{
    for (auto element : vector) {
        os << element << " ";
    }
    return os;
}

// Define a custom allocator
template <typename T>
class CustomAllocator : public std::allocator<T>
{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;

    pointer allocate(std::size_t n)
    {
        return static_cast<pointer>(::operator new(n * sizeof(T)));
    }

    void deallocate(pointer p, std::size_t n)
    {
        ::operator delete(p, n * sizeof(T));
    }
};

// Define a custom vector type using the custom allocator
template <typename T>
using CustomVector = std::vector<T, CustomAllocator<T>>;

// Define the MazeSolver class, derived from rclcpp::Node
class MazeSolver : public rclcpp::Node
{
public:
    // Constructor
    MazeSolver() : Node("maze_solver_node")
    {
        // Create publishers
        path_publisher_ = this->create_publisher<std_msgs::msg::Float64MultiArray>("/path_array", 10);

        // Create subscribers
        obsx_subscription_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
                "/obsx", 10, std::bind(&MazeSolver::obsxCallback, this, std::placeholders::_1));

        obsy_subscription_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
                "/obsy", 10, std::bind(&MazeSolver::obsyCallback, this, std::placeholders::_1));

        gridsize_subscription_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
                "/gridsize", 10, std::bind(&MazeSolver::gridsizeCallback, this, std::placeholders::_1));

        timer_ = this->create_wall_timer(std::chrono::milliseconds(1000), std::bind(&MazeSolver::publishData, this));
    }

    // Callback function for obsx subscription
    void obsxCallback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
    {
        obsx_ = std::move(*msg);
    }

    // Callback function for obsy subscription
    void obsyCallback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
    {
        obsy_ = std::move(*msg);
    }

    // Callback function for gridsize subscription
    void gridsizeCallback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
    {
        gridsize_ = std::move(*msg);
    }

    // Function to publish data
    bool isCellOnLine(int x1, int y1, int x2, int y2)//obsx,obsy,x,y
    {
        const int dx = std::abs(x2 - x1);
        const int dy = std::abs(y2 - y1);
        const int sx = (x1 < x2) ? 1 : -1;
        const int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        // Bounding box coordinates
        const int xmin = std::min(x1, x2);
        const int xmax = std::max(x1, x2);
        const int ymin = std::min(y1, y2);
        const int ymax = std::max(y1, y2);

        // Iterate over cells within the bounding box only
        for (int x = xmin; x <= xmax; x++)
        {
            for (int y = ymin; y <= ymax; y++)
            {
                // Check if the cell lies on the line segment
                if (x == x1 && y == y1)
                {
                    return true;  // The cell lies on the line segment
                }

                int e2 = 2 * err;
                if (e2 > -dy)
                {
                    err -= dy;
                    x1 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y1 += sy;
                }
            }
        }

        return false;  // No cell on the line segment
    }


    void publishData()
    {
        // Create and populate the message



        const int ROWS = gridsize_.data[1];
        const int COLS = gridsize_.data[0];

        std::vector<std::vector<Node_s*>> grid(ROWS, std::vector<Node_s*>(COLS));

        // Initialize grid and obstacles
        for (int i = 0; i < ROWS; i++)
        {
            for (int j = 0; j < COLS; j++)
            {

                grid[i][j] = std::move(new Node_s(i, j));
            }
        }

        const int vehicleWidth = 10;  // Define the width of the vehicle
        const int vehicleLength = 12; // Define the length of the vehicle

        for (int i = 0; i < obsx_.data.size(); i++)
        {
            const int obsx = std::move(obsx_.data[i]);
            const int obsy = std::move(obsy_.data[i]);

            // Mark the cells along the line segment representing the vehicle's dimensions
            for (int dx = -vehicleWidth; dx <= vehicleWidth; dx++) {
                for (int dy = -vehicleLength; dy <= vehicleLength; dy++) {
                    const int x = obsx + dx;
                    const  int y = obsy + dy;

                    // Check if the cell lies on the line segment
                    if (isCellOnLine(obsx, obsy, x, y)) {
                        if (x >= 0 && x < ROWS && y >= 0 && y < COLS) {
                            grid[x][y]->obstacle = 1;
                        }
                    }
                }
            }
        }

        Node_s* startNode = grid[gridsize_.data[2]][gridsize_.data[3]];
        Node_s* endNode = grid[gridsize_.data[4]][gridsize_.data[5]];

        std::vector<Node_s*> path = AStar(startNode, endNode, grid);

        // Clear path_msg->data before populating
        path_msg->data.clear();

        if (!path.empty())
        {
            std::cout << "Path found:" << std::endl;

            std::vector<std::vector<double>> path_array;
            path_array.reserve(path.size());

            for (Node_s* node : path)
            {
                std::cout << "(" << node->x << ", " << node->y << ")" << std::endl;
                path_array.push_back(std::move(std::vector<double>{node->x, node->y}));
            }

            // Set the dimensions of the path message
            path_msg->layout.dim.clear();
            path_msg->layout.dim.push_back(std_msgs::msg::MultiArrayDimension());
            path_msg->layout.dim.push_back(std_msgs::msg::MultiArrayDimension());
            path_msg->layout.dim[0].label = "rows";
            path_msg->layout.dim[0].size = path_array.size();
            path_msg->layout.dim[0].stride = path_array.size() * 2;
            path_msg->layout.dim[1].label = "cols";
            path_msg->layout.dim[1].size = 2;
            path_msg->layout.dim[1].stride = 2;

            // Flatten the 2D vector into a 1D array
            for (const auto& point : path_array)
            {
                path_msg->data.insert(path_msg->data.end(), point.begin(), point.end());
            }
            std::ofstream outFile("path.txt");
            if (outFile.is_open())
            {
                for (size_t i = 0; i < path_msg->data.size(); i += 2) {
                    outFile << path_msg->data[i] << "," << path_msg->data[i + 1] << "\n";
                }
                outFile.close();
                std::cout << "Path written to path.txt" << std::endl;
            }
            else
            {
                std::cout << "Failed to open path.txt for writing" << std::endl;
            }

        }
        else
        {
            std::cout << "Path not found." << std::endl;
        }

        path_publisher_->publish(*path_msg);
    }



    // Destructor
    ~MazeSolver()
    {
        // Clean up memory
        for (int i = 0; i < ROWS; i++) {
            for (int j = 0; j < COLS; j++) {
                delete grid[i][j];
            }
        }
    }

private:
    // ROS subscribers
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr obsx_subscription_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr obsy_subscription_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr gridsize_subscription_;

    // ROS publisher
    rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr path_publisher_;

    // ROS timer
    rclcpp::TimerBase::SharedPtr timer_;

    // ROS messages for data exchange
    std_msgs::msg::Float64MultiArray gridsize_;
    std_msgs::msg::Float64MultiArray obsx_;
    std_msgs::msg::Float64MultiArray obsy_;

    // Constants for grid size
    const int ROWS = 0; // Update with actual ROWS value
    const int COLS = 0; // Update with actual COLS value

    // Grid of nodes
    std::vector<std::vector<Node_s*, CustomAllocator<Node_s*>>> grid;
    std::shared_ptr<std_msgs::msg::Float64MultiArray> path_msg = std::make_shared<std_msgs::msg::Float64MultiArray>();

};
#endif //CPP_PUBSUB_MAZESOLVER_H