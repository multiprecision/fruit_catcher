// Digital Image Processing Final Project
// Copyright (C) 2016 Samuel I. Gunadi
// All rights reserved.

#define _USE_MATH_DEFINES
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include <vector>
#include <array>
#include <iostream>
#include <chrono>
#include <random>
#include <cmath>

#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>

#include "asset.h"

static void mouse_callback(int event, int x, int y, int flags, void* userdata);


class Fruit
{
public:
	sf::RectangleShape rect;
	float falling_speed;
	bool caught;

	Fruit(const sf::Texture* tex, float speed)
	{
		rect.setSize(sf::Vector2f(60, 60));
		rect.setTexture(tex);
		falling_speed = speed;
		caught = false;
	};
};

// variables used in mouse callback function

// the image from camera in hsv colorspace
cv::Mat hsv_img;
// indicate whether the color of the circles is set or not
std::array<bool, 2> color_set{ false, false };
// the hsv color ranges used in circle detection
std::array<std::array<cv::Vec3b, 2>, 2> color_ranges;

enum class Game_state { SETUP, START, PLAYING, END };
Game_state game_state = Game_state::SETUP;

// program main entry point

int main(int argc, char** argv)
{
	// video capture device
	cv::VideoCapture capture(0);
	// kernel for erosion and dilation
	cv::Mat element = cv::getStructuringElement(cv::MorphShapes::MORPH_ELLIPSE, cv::Size(5, 5));
	// the image from camera
	cv::Mat source_img;

	std::array<float, 2> circle_radius;
	std::array<cv::Point2f, 2> circle_pos;
	std::array<cv::Mat, 2> mask_img;
	// used for measuring FPS
	std::chrono::time_point<std::chrono::steady_clock> start_clock;
	std::chrono::time_point<std::chrono::steady_clock> end_clock;
	std::chrono::duration<long long, std::nano> clock_diff;
	// indicate whether there's next frame from the camera
	bool next_frame = true;
	// used in game logic
	float angle = 0;
	float player_pos = 0;
	unsigned fruit_speed_min = 100;
	unsigned fruit_speed_max = 300;
	unsigned fruit_height_max = 2000;
	int score = 0;
	// time limit in seconds
	double time_limit = 30;
	double time_remaining = 0;
	sf::Clock clock;
	std::vector<Fruit> fruits;
	int fruits_num = 50;

	// game window
	float game_window_height = 540; // qHD
	float game_window_width = 960;
	sf::RenderWindow game_window(sf::VideoMode((unsigned int)game_window_width, (unsigned int)game_window_height), "Fruit Catcher Game");

	// load game resources
	sf::Font main_font;
	main_font.loadFromMemory(inconsolata_font, 58464);

	sf::Texture bg_tex;
	if (!bg_tex.loadFromFile("../asset/background.jpg"))
		return EXIT_FAILURE;
	sf::Texture basket_tex;
	if (!basket_tex.loadFromFile("../asset/basket.png"))
		return EXIT_FAILURE;

	sf::RectangleShape bg_rect(sf::Vector2f(game_window_width, game_window_height));
	bg_rect.setTexture(&bg_tex);
	sf::RectangleShape basket_rect(sf::Vector2f(100, 100));
	basket_rect.setTexture(&basket_tex);

	std::array<sf::Texture, 8> fruit_tex;
	if (!fruit_tex[0].loadFromFile("../asset/apple.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[1].loadFromFile("../asset/orange.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[2].loadFromFile("../asset/banana.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[3].loadFromFile("../asset/blueberry.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[4].loadFromFile("../asset/strawberry.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[5].loadFromFile("../asset/peach.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[6].loadFromFile("../asset/cherry.png"))
		return EXIT_FAILURE;
	if (!fruit_tex[7].loadFromFile("../asset/grape.jpg"))
		return EXIT_FAILURE;

	// set up opencv windows and mouse event handler
	cv::namedWindow("camera_input_window");
	cv::setMouseCallback("camera_input_window", mouse_callback, 0);
	
	// set up RNG
	std::mt19937 rng;
	rng.seed(std::random_device()());

	while (next_frame && game_window.isOpen())
	{
		// clear the window with black color
		game_window.clear(sf::Color::Black);
		// for fps counter
		start_clock = std::chrono::steady_clock::now();
		// get next frame from camera
		next_frame = capture.read(source_img);
		if (!next_frame)
		{
			break;
		}
		// convert to hsv colorspace
		// NOTE: OpenCV hue range is 0 to 180, not 0 to 360!
		cv::cvtColor(source_img, hsv_img, cv::COLOR_BGR2HSV);
		// detect the 2 circles
		for (int i = 0; i < 2; i++)
		{
			// create a mask for the respective color then perform 2 iterations of dilations and erosions to remove any small blobs left in the mask
			cv::inRange(hsv_img, color_ranges[i][0], color_ranges[i][1], mask_img[i]);
			cv::morphologyEx(mask_img[i], mask_img[i], cv::MorphTypes::MORPH_OPEN, element, cv::Point(-1, -1), 2);
			// find contours in the mask
			std::vector<std::vector<cv::Point>> contours;
			cv::findContours(mask_img[i].clone(), contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
			// find the largest contour in the mask, then use it to compute the minimum enclosing circle and centroid
			if (!contours.empty())
			{
				double largest_area = 0;
				size_t largest_contour_index = 0;
				for (size_t i = 0; i < contours.size(); i++) // iterate through each contour. 
				{
					double a = contourArea(contours[i], false); // find the area of contour
					if (a > largest_area)
					{
						largest_area = a;
						largest_contour_index = i; // store the index of largest contour
					}
				}
				// get circle radius and centroid
				cv::minEnclosingCircle(contours[largest_contour_index], circle_pos[i], circle_radius[i]);
				// draw the circle as an indicator
				cv::circle(source_img, circle_pos[i], static_cast<int>(circle_radius[i]), i == 0 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0), 5);
			}
		}
		// find the angle between 2 points
		angle = std::atan2(circle_pos[1].y - circle_pos[0].y, circle_pos[1].x - circle_pos[0].x);

		cv::imshow("camera_input_window", source_img);
#ifdef _DEBUG
		cv::imshow("mask0", mask_img[0]);
		cv::imshow("mask1", mask_img[1]);
#endif
		switch (game_state)
		{
		case Game_state::SETUP:
		{
			sf::Text text("Welcome to Fruit Catcher game.\r\nClick the 2 circles in the camera_input window.\r\nClick the circle on the left first,\r\nand then click the circle on the right.\r\n\r\nDigital Image Processing Project by Samuel", main_font);
			text.setColor(sf::Color::White);
			game_window.draw(text);
			break;
		}
		case Game_state::START:
		{
			sf::Text text("Press space key to begin.", main_font);
			text.setColor(sf::Color::White);
			game_window.draw(text);
			break;
		}
		case Game_state::PLAYING:
		{

			time_remaining = time_limit - clock.getElapsedTime().asSeconds();
			game_window.draw(bg_rect);
			if (angle < 0)
			{
				player_pos = 0.5f + 0.5f * (-angle / float(M_PI_2));
			}
			else
			{
 				player_pos = 0.5f - 0.5f * (angle / float(M_PI_2));
			}
			basket_rect.setPosition(player_pos * game_window_width, game_window_height - 80);
			for (Fruit &e : fruits)
			{
			}

			end_clock = std::chrono::steady_clock::now();
			auto time_elapsed = end_clock - start_clock;
			sf::Rect<float> basket_bounding_box(basket_rect.getPosition(), basket_rect.getSize());
			for (Fruit &e : fruits)
			{
				// determine whether the fruits were caught or not
				sf::Rect<float> fruit_bounding_box(e.rect.getPosition(), e.rect.getSize());
				if (basket_bounding_box.intersects(fruit_bounding_box))
				{
					if (!e.caught)
					{
						e.caught = true;
						score++;
					}
				}
				// draw the fruit if not caught
				if (!e.caught)
				{
					game_window.draw(e.rect);
				}
				// update falling fruit location
				e.rect.setPosition(e.rect.getPosition() + sf::Vector2f(0.f, e.falling_speed * 1e-9f * time_elapsed.count()));

			}
			// draw score. note: draw this last
			game_window.draw(basket_rect);
			sf::Text text("Time: " + std::to_string(time_remaining) + " Score: " + std::to_string(score), main_font);
			text.setColor(sf::Color::White);
			game_window.draw(text);

			if (time_remaining <= 0)
			{
				game_state = Game_state::END;
			}
			break;

		}
		case Game_state::END:
		{
			sf::Text text("Game over! Your score is " + std::to_string(score) + "!\r\nPress space key to play again.", main_font);
			text.setColor(sf::Color::White);
			game_window.draw(text);
			break;
		}
		}

		// diplay
		game_window.display();

		end_clock = std::chrono::steady_clock::now();
		clock_diff = end_clock - start_clock;
		game_window.setTitle("Fruit Catcher Game | FPS: " + std::to_string(1.0 / (1e-9 * clock_diff.count())));

		sf::Event event;
		while (game_window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				game_window.close();
			else if (event.type == sf::Event::KeyPressed)
			{
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
				{
					if (game_state == Game_state::START)
					{
						game_state = Game_state::PLAYING;
						clock.restart();
						score = 0;
						fruits.clear();
						// generate fruits randomly
						std::uniform_int_distribution<std::mt19937::result_type> tex_dist(0, static_cast<unsigned int>(fruit_tex.size()) - 1);
						std::uniform_int_distribution<std::mt19937::result_type> pos_x_dist(0, static_cast<unsigned int>(game_window_width));
						std::uniform_int_distribution<std::mt19937::result_type> pos_y_dist(1, fruit_height_max);
						std::uniform_int_distribution<std::mt19937::result_type> speed_dist(fruit_speed_min, fruit_speed_max);
						for (int i = 0; i < fruits_num; i++)
						{
							Fruit fruit(&fruit_tex[tex_dist(rng)], (float) speed_dist(rng));
							fruit.rect.setPosition(static_cast<float>(pos_x_dist(rng)), 0.f - pos_y_dist(rng));
							fruits.push_back(fruit);
						}

					}
					if (game_state == Game_state::END)
					{
						game_state = Game_state::START;
					}
				}
			}
		}
		// This function is the only method in HighGUI that can fetch and handle events, so it needs to be
		// called periodically for normal event processing unless HighGUI is used within an environment that
		// takes care of event processing.
		cv::waitKey(1);
	}
	return EXIT_SUCCESS;
}

// mouse event handler

static void mouse_callback(int event, int x, int y, int flags, void* userdata)
{
	// continue only if the left mouse button was pressed
	if (event != cv::EVENT_LBUTTONDOWN) return;
	// set colors for each circle
	for (int i = 0; i < 2; i++)
	{
		if (!color_set[i])
		{
			cv::Vec3i color = hsv_img.at<cv::Vec3b>(y, x);
			color_ranges[i][0] = cv::Vec3b(color[0] - 8 < 0 ? 0 : color[0] - 8,
				color[1] - 80 < 0 ? 0 : color[1] - 80,
				color[2] - 80 < 0 ? 0 : color[2] - 80);
			color_ranges[i][1] = cv::Vec3b(color[0] + 8 > 150 ? 255 : color[0] + 8,
				color[1] + 80 > 255 ? 255 : color[1] + 80,
				color[2] + 80 > 255 ? 255 : color[2] + 80);
			color_set[i] = true;
			if (i == 1) // if both colors are set
			{
				game_state = Game_state::START;
			}

#ifdef _DEBUG
			std::cout << i << "> color: " << color << " color_min: " << color_ranges[i][0] << " color_max " << color_ranges[i][1] << std::endl;
#endif // DEBUG
			break;
		}
	}

}