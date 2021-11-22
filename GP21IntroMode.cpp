#include "GP21IntroMode.hpp"

#include "GL.hpp"
#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

#include <algorithm>
#include <random>
#include <fstream>

GP21IntroMode::GP21IntroMode(std::shared_ptr< Mode > const &next_mode_) : next_mode(next_mode_) {
	{ // ------ music ------
		std::vector< float > data(10 * 48000, 0.0f);

		//triangle wave:
		[[maybe_unused]] auto triangle01 = [](float t) -> float {
			t += 0.25f;
			return std::abs(4.0f * (t - std::floor(t)) - 2.0f) - 1.0f;
		};
		//extra crunchy 512-entry-table, 6-bit sine wave:
		[[maybe_unused]] auto sine01 = [](float t) -> float {
			t -= std::floor(t);
			t = std::round(t * 512.0f) / 512.0f;
			return std::round( std::sin(t * 2.0f * M_PI) * 1.0f ) / 1.0f;
		};

		constexpr float Attack  = 0.02f;
		constexpr float Decay   = 0.0f;
		constexpr float Sustain = 1.0f;
		constexpr float Release = 0.1f;

		//play synth at given time for given length at given frequency:
		auto tone = [&](float start, float len, float hz, float vol){
			int32_t begin = start * 48000;
			int32_t end = begin + (len + Release) * 48000;

			for (int32_t sample = begin; sample < end; ++sample) {
				float t = (sample - begin + 0.5f) / 48000.0f;

				//envelope value:
				float env;
				if (t < Attack) env = t / Attack;
				else if (t < Attack + Decay) env = ((t - Attack) / Decay) * (Sustain - 1.0f) + 1.0f;
				else if (t <= len) env = Sustain;
				else env = std::min(1.0f, (t - len) / Release) * (0.0f - Sustain) + Sustain;

				//osc2:
				[[maybe_unused]] float osc2 = triangle01(t * hz * 0.99f);

				//osc1 (with FM... well, PM):
				float osc1 = triangle01((t + osc2 * 0.005f) * hz);

				data[sample] += vol * env * osc1;
			}
		};

		//midi note number to frequency (based on A4 being 440hz)
		auto midi2hz = [](float midi) -> float {
			return 440.0f * std::exp2( (midi - 69.0f) / 12.0f );
		};

		[[maybe_unused]] auto C = [](int32_t oct) { return 12 + 12 * oct; };
		[[maybe_unused]] auto Cs= [](int32_t oct) { return 13 + 12 * oct; };
		[[maybe_unused]] auto D = [](int32_t oct) { return 14 + 12 * oct; };
		[[maybe_unused]] auto Ds= [](int32_t oct) { return 15 + 12 * oct; };
		[[maybe_unused]] auto E = [](int32_t oct) { return 16 + 12 * oct; };
		[[maybe_unused]] auto F = [](int32_t oct) { return 17 + 12 * oct; };
		[[maybe_unused]] auto Fs= [](int32_t oct) { return 18 + 12 * oct; };
		[[maybe_unused]] auto G = [](int32_t oct) { return 19 + 12 * oct; };
		[[maybe_unused]] auto Gs= [](int32_t oct) { return 20 + 12 * oct; };
		[[maybe_unused]] auto A = [](int32_t oct) { return 21 + 12 * oct; };
		[[maybe_unused]] auto As= [](int32_t oct) { return 22 + 12 * oct; };
		[[maybe_unused]] auto B = [](int32_t oct) { return 23 + 12 * oct; };

		tone( 0.0f, 1.0f / 4.0f, midi2hz( C(4) ), 0.5f );
		tone( 0.0f, 1.0f / 4.0f, midi2hz( E(4) ), 0.5f );
		tone( 0.0f, 1.0f / 4.0f, midi2hz( G(4) ), 0.5f );

		tone( 0.5f, 1.0f / 4.0f, midi2hz( D(4) ), 0.5f );
		tone( 1.0f, 1.0f / 4.0f, midi2hz( E(4) ), 0.5f );
		tone( 1.5f, 1.0f / 4.0f, midi2hz( F(4) ), 0.5f );
		tone( 2.0f, 1.0f / 4.0f, midi2hz( G(4) ), 0.5f );
		tone( 2.5f, 1.0f / 4.0f, midi2hz( A(4) ), 0.5f );
		tone( 3.0f, 1.0f / 4.0f, midi2hz( B(4) ), 0.5f );
	
	
		//some sort of resonant low pass filter as per:
		// http://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
		[[maybe_unused]] auto lowpass = [](std::vector< float > &data, float hz) {

			float Q = 0.5f;

			float omega = 2 * M_PI * hz / 48000.0f;
			float alpha = std::sin(omega) / (2.0f * Q);

			float b0 = (1 - std::cos(omega)) / 2.0f;
			float b1 = 1 - std::cos(omega);
			float b2 = (1 - std::cos(omega)) / 2.0f;
			float a0 = 1.0f + alpha;
			float a1 = -2.0f * std::cos(omega);
			float a2 = 1.0f - alpha;

			b0 /= a0;
			b1 /= a0;
			b2 /= a0;
			a1 /= a0;
			a2 /= a0;

			float x1 = 0.0f;
			float x2 = 0.0f;
			float y1 = 0.0f;
			float y2 = 0.0f;
			for (float &x0 : data) {
				float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
				x2 = x1;
				x1 = x0;
				y2 = y1;
				y1 = y0;

				x0 = y0;
			}

		};

		lowpass(data, 4000.0f);
		/*lowpass(data, 4000.0f);*/

		{ //run a basic 'digital reverb' over stuff:
			//use a delay line with a few different taps and feedbacks:
			std::array< float, 2 * 48000 > delay;
			for (auto &s : delay) s = 0.0f; //clear delay line
			uint32_t head = 0;
			auto tap = [&](float offset) -> float {
				return delay[(head + int32_t(delay.size()) - int32_t(std::floor(offset * 48000.0f))) % delay.size()];
			};
			for (float &s : data) {
				float wet = 0.5f * s + (1.0f * tap(0.1f) + 1.0f * tap(0.11f) + 1.0f * tap(0.05f)) / 4.0f;
				delay[head] = wet;
				head = (head + 1) % delay.size();
				s = wet; //DEBUG -- should mix?
			}
		}



		{ //DEBUG:
			std::ofstream dump("music-dump.f32", std::ios::binary);
			dump.write(reinterpret_cast< const char * >(data.data()), data.size() * 4);
		}

		music_sample = std::make_unique< Sound::Sample >(data);

		music = Sound::play(*music_sample);
	}

	// ------ shader ------
	//(based on ColorProgram.cpp)
	color_program = gl_compile_program(
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"in vec4 Position;\n"
		"in vec4 Color;\n"
		"out vec4 color;\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	color = Color;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"in vec4 color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	fragColor = color;\n"
		"}\n"
	);

	//look up the locations of vertex attributes:
	GLuint Position_vec4 = glGetAttribLocation(color_program, "Position");
	GLuint Color_vec4 = glGetAttribLocation(color_program, "Color");

	//look up the locations of uniforms:
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(color_program, "OBJECT_TO_CLIP");

	// ------ vertex buffer + vertex array object ------
	// (based on DrawLines.cpp)

	glGenVertexArrays(1, &vertex_buffer_for_color_program);
	glBindVertexArray(vertex_buffer_for_color_program);

	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	glVertexAttribPointer(
		Position_vec4, //attribute
		2, //size
		GL_FLOAT, //type
		GL_FALSE, //normalized
		sizeof(GP21IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP21IntroMode::Vertex, Position) //offset
	);
	glEnableVertexAttribArray(Position_vec4);

	glVertexAttribPointer(
		Color_vec4, //attribute
		4, //size
		GL_UNSIGNED_BYTE, //type
		GL_TRUE, //normalized
		sizeof(GP21IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP21IntroMode::Vertex, Color) //offset
	);
	glEnableVertexAttribArray(Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	GL_ERRORS();

	// ------ gp21 logo ------

	auto add_cubes = [this](glm::ivec3 const &upper_left, glm::ivec3 const &col_dir, std::string const &img) {
		int32_t row = 0;
		int32_t col = 0;
		for (char c : img) {
			if (c == '#') {
				Cube cube;
				cube.target = upper_left + row * glm::ivec3(0,0,-1) + col * col_dir;
				cubes.emplace_back(cube);
			}
			if (c == '\n') {
				row += 1;
				col = 0;
			} else {
				col += 1;
			}
		}
	};
	add_cubes(glm::ivec3(-4,-3,4), glm::ivec3(1,0,0),
		" # \n"
		"# #\n"
		" ##\n"
		"  #\n"
		"# #\n"
		" # \n"
	);
	add_cubes(glm::ivec3(-1,-2,2), glm::ivec3(0,1,0),
		"## \n"
		"# #\n"
		"## \n"
		"#  \n"
		"#  \n"
	);

	add_cubes(glm::ivec3(0,0,4), glm::ivec3(1,0,0),
		" # \n"
		"# #\n"
		"  #\n"
		" # \n"
		"#  \n"
		"###\n"
	);
	add_cubes(glm::ivec3(1,2,4), glm::ivec3(0,1,0),
		" # \n"
		"## \n"
		" # \n"
		" # \n"
		" # \n"
		"###\n"
	);

	//depth sort cubes:
	std::sort(cubes.begin(), cubes.end(), [](Cube const &a, Cube const &b){
		if (a.target.x != b.target.x) return a.target.x < b.target.x; //smaller x first
		else if (a.target.y != b.target.y) return a.target.y > b.target.y; //at same x, larger y first
		else return a.target.z < b.target.z; //at same x,y: smaller z first
	});

	//set up 'animation start' key values for each cube:

	float min_key = std::numeric_limits< float >::infinity();
	float max_key = -std::numeric_limits< float >::infinity();
	for (auto &cube : cubes) {
		cube.at = cube.target.z + 20.0f;
		cube.velocity = -4.0f;

		cube.key = cube.target.x + 0.7f * cube.target.y + 0.4f * cube.target.z;
		min_key = std::min(min_key, cube.key);
		max_key = std::max(max_key, cube.key);
	}

	//normalize keys to [0,1] range:
	for (auto &cube : cubes) {
		cube.key = (cube.key - min_key) / (max_key - min_key);
	}
}

GP21IntroMode::~GP21IntroMode() {
}

bool GP21IntroMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	/* Eventually:
	if (evt.type == SDL_KEYDOWN) {
		//on any key press, skip the rest of the intro:
		music->set_volume(0.0f, 1.0f / 10.0f);
		Mode::set_current(next_mode);
		return true;
	}*/
	return false;
}

void GP21IntroMode::update(float elapsed) {
	time += elapsed;
	if (time > 10.0f) {
		time -= 10.0f;
	}

	//handle cubes falling:

	float fall_on_key = (time - 0.1f) / 1.4f;

	float fall_off_key = (time - 6.0f) / 2.4f;

	constexpr float Gravity = -25.0f;
	std::unordered_map< glm::ivec2, Cube * > tops;
	for (Cube &cube : cubes) {
		//if cube isn't falling on yet, default values:
		if (fall_on_key < cube.key) {
			cube.velocity = -4.0f;
			cube.at = cube.target.z + 20.0f;
			continue;
		}

		//will offset target z when cube is marked to fall off:
		int32_t target_z = cube.target.z;
		if (fall_off_key > cube.key) {
			target_z -= 100;
		}

		if (cube.at > target_z) {
			cube.velocity += Gravity * elapsed;
		}
		cube.at += cube.velocity * elapsed;

		//cube-ground collisions:
		if (cube.at < target_z) {
			cube.at = target_z;
			if (cube.velocity < 0.0f) {
				cube.velocity = std::max(0.0f, -0.5f * cube.velocity - 0.5f);
			}
		}

		//cube-cube collisions:
		auto f = tops.find(glm::ivec2(cube.target));
		if (f != tops.end()) {
			Cube &below = *f->second;
			if (below.at + 1.0f > cube.at) {
				float rel = below.velocity - cube.velocity;
				if (rel > 0.0f) {
					below.velocity -= 0.75f * rel;
					cube.velocity += 0.75f * rel;
				}
			}
		}

		tops[glm::ivec2(cube.target)] = &cube;
	}


}

void GP21IntroMode::draw(glm::uvec2 const &drawable_size) {
	//requested visible bounds:
	glm::vec2 scene_min = glm::vec2(-9.0f, -4.0f);
	glm::vec2 scene_max = glm::vec2( 7.0f,  6.5f);

	{ //actually, zoom those bounds out a bit:
		glm::vec2 center = 0.5f * (scene_min + scene_max);
		glm::vec2 radius = 0.5f * (scene_max - scene_min);
		scene_min = center - 2.0f * radius;
		scene_max = center + 2.0f * radius;
	}

	//computed visible bounds:
	glm::vec2 screen_min, screen_max;

	//compute matrix for vertex shader:
	glm::mat4 object_to_clip;

	{ //make sure scene_min - scene_max fits in drawable_size:
		float aspect = drawable_size.x / float(drawable_size.y);
		float scale = glm::min(
			2.0f * aspect / (scene_max.x - scene_min.x),
			2.0f / (scene_max.y - scene_min.y)
		);
		object_to_clip = glm::mat4(
			scale / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, scale, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			scale / aspect * -0.5f * (scene_max.x + scene_min.x), scale * -0.5f * (scene_max.y + scene_min.y), 0.0f, 1.0f
		);

		screen_min = 0.5f * (scene_max + scene_min) - glm::vec2(aspect, 1.0f) / scale;
		screen_max = 0.5f * (scene_max + scene_min) + glm::vec2(aspect, 1.0f) / scale;
	}

	//The oh-so-critical class colors:
	const glm::u8vec4 Fg1 = glm::u8vec4(0xf2, 0xd2, 0xb6, 0xff);
	const glm::u8vec4 Fg2 = glm::u8vec4(0xf2, 0xad, 0x94, 0xff);
	const glm::u8vec4 Fg3 = glm::u8vec4(0xf2, 0x89, 0x72, 0xff);
	const glm::u8vec4 Bg1 = glm::u8vec4(0xf4, 0xf7, 0xf5, 0xff);

	//compute triangles to draw:
	std::vector< Vertex > attribs;
	
	//--- stripes ---

	//helper for rectangles:
	auto draw_rectangle = [&attribs](glm::vec2 min, glm::vec2 max, glm::u8vec4 color) {
		if (!attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(glm::vec2(min.x, min.y), color);
		if (attribs.size() != 1) attribs.emplace_back(attribs.back());
		attribs.emplace_back(glm::vec2(min.x, max.y), color);
		attribs.emplace_back(glm::vec2(max.x, min.y), color);
		attribs.emplace_back(glm::vec2(max.x, max.y), color);
	};

	//draw_rectangle(scene_min, scene_max, glm::u8vec4(0xff, 0x00, 0xff, 0xff)); //DEBUG (show scene bounds)

	auto stripe = [&](float on_amt, float off_amt, float y, glm::u8vec4 color) {
		float min_x = glm::mix(screen_min.x, screen_max.x, glm::clamp(off_amt, 0.0f, 1.0f));
		float max_x = glm::mix(screen_min.x, screen_max.x, glm::clamp(on_amt, 0.0f, 1.0f));
		draw_rectangle(glm::vec2(min_x, y), glm::vec2(max_x, y+1), color);
	};

	stripe( (time - 0.3f) / 0.4f,  (time - 7.6f) / 1.1f,  3.0f, Fg1);
	stripe( (time - 0.2f) / 0.4f,  (time - 7.7f) / 1.1f,  2.0f, Fg2);
	stripe( (time - 0.0f) / 0.4f,  (time - 7.8f) / 1.1f,  1.0f, Fg3);
	stripe( (time - 0.1f) / 0.4f,  (time - 7.9f) / 1.1f,  0.0f, Fg1);

	//--- cubes ---
	glm::mat4x2 cubes_to_world = glm::mat4x2(
		glm::vec2( std::cos(M_PI / 6.0f),-std::sin(M_PI / 6.0f)),
		glm::vec2( std::cos(M_PI / 6.0f), std::sin(M_PI / 6.0f)),
		glm::vec2( 0.0f, 1.0f),
		glm::vec2( 0.0f, 0.0f)
	);
	auto draw_cube = [&](GP21IntroMode::Cube const &cube) {
		glm::vec2 c = cubes_to_world * glm::vec4(cube.target.x, cube.target.y, cube.at, 1.0f);
		glm::vec2 rx = 0.5f * cubes_to_world[0];
		glm::vec2 ry = 0.5f * cubes_to_world[1];
		glm::vec2 rz = 0.5f * cubes_to_world[2];

		if (!attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c + rz - rx - ry, Fg1);
		if (attribs.size() != 1) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c + rz - rx + ry, Fg1);
		attribs.emplace_back(c + rz + rx - ry, Fg1);
		attribs.emplace_back(c + rz + rx + ry, Fg1);

		if (!attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c - ry - rx - rz, Fg2);
		if (attribs.size() != 1) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c - ry - rx + rz, Fg2);
		attribs.emplace_back(c - ry + rx - rz, Fg2);
		attribs.emplace_back(c - ry + rx + rz, Fg2);

		if (!attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c + rx - ry - rz, Fg3);
		if (attribs.size() != 1) attribs.emplace_back(attribs.back());
		attribs.emplace_back(c + rx - ry + rz, Fg3);
		attribs.emplace_back(c + rx + ry - rz, Fg3);
		attribs.emplace_back(c + rx + ry + rz, Fg3);
	};

	for (Cube const &cube : cubes) {
		draw_cube(cube);
	}

	//masking at top in case of very thin/tall windows:
	{
		glm::u8vec4 c0 = glm::u8vec4(Bg1.r, Bg1.g, Bg1.b, 0x00);
		glm::u8vec4 c1 = Bg1;
		glm::u8vec4 c2 = Bg1;
		if (!attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(glm::vec2(screen_min.x, 14.0f), c0);
		if (attribs.size() != 1) attribs.emplace_back(attribs.back());
		attribs.emplace_back(glm::vec2(screen_max.x, 14.0f), c0);
		attribs.emplace_back(glm::vec2(screen_min.x, 16.0f), c1);
		attribs.emplace_back(glm::vec2(screen_max.x, 16.0f), c1);
		attribs.emplace_back(glm::vec2(screen_min.x, 27.0f), c2);
		attribs.emplace_back(glm::vec2(screen_max.x, 27.0f), c2);
	}

	//----- actually draw ----
	// (Based on DrawLines.cpp)

	//upload attribs:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW); //upload attribs array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Clear background:
	glClearColor(Bg1.r / 255.0f, Bg1.g / 255.0f, Bg1.b / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//No depth test, please, but yes let's use alpha blending:
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(color_program);
	glUniformMatrix4fv(OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(object_to_clip));

	glBindVertexArray(vertex_buffer_for_color_program);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(attribs.size()));
	glBindVertexArray(0);

	glUseProgram(0);

	GL_ERRORS();
}
