#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

GLuint toilet_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > toilet_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("toilet.pnct"));
	toilet_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

GLuint margit_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > margit_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("mergertthefell.pnct"));
	margit_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > toilet_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("toilet.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = toilet_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = toilet_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

Load< Scene > margit_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("mergertthefell.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = margit_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = margit_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

Load< Sound::Sample > margit_music(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("mergertacapella.wav"));
});


Load< Sound::Sample > flush(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("flush.wav"));
});

PlayMode::PlayMode() : scene(*toilet_scene) {
	//toilet scene is loaded

	//load margit
	scene.transforms.emplace_back();
    margit_model = &scene.transforms.back();
    margit_model->position = glm::vec3(0.0f, 0.0f, -50.0f); // adjust as needed
	margit_model->scale = glm::vec3(2.5f, 2.5f, 3.0f);

    Mesh const &mesh = margit_meshes->lookup("Margit");
    scene.drawables.emplace_back(margit_model);
    Scene::Drawable &drawable = scene.drawables.back();

    drawable.pipeline = lit_color_texture_program_pipeline;
    drawable.pipeline.vao   = margit_meshes_for_lit_color_texture_program;
    drawable.pipeline.type  = mesh.type;
    drawable.pipeline.start = mesh.start;
    drawable.pipeline.count = mesh.count;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start music loop playing:
	// (note: position will be over-ridden in update())
	margit_loop = Sound::loop_3D(*margit_music, 1.0f, glm::vec3(0.0f, 0.0f, 0.0f), 10.0f);
}

PlayMode::~PlayMode() {
}

//global game state variables
bool GameOver = false;
bool GameStart = true;
bool DeathAnimation = false;
//game mechanic const values
float StartTimer = 5.0f;
float PeeTimerMax = 15.0f;
float PeeTimer = 15.0f; //must be global so we can draw later
float DeathTimer = 5.0f;
float CameraHeight = 7.0f;
float MinAwayDistance = 100.0f;
float MaxAwayDistance = 150.0f;
glm::vec3 margit_pos; //used to derive mesh and music position

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	//check game state
	if (GameOver) {
		if (evt.type == SDL_EVENT_KEY_DOWN) {
			if (evt.key.key == SDLK_ESCAPE) {
				SDL_SetWindowRelativeMouseMode(Mode::window, false);
				return true;
			}
		}
		else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
				SDL_SetWindowRelativeMouseMode(Mode::window, true);
				return true;
			}
		}
		//exit early and don't allow any other action
		return false;
	}

	//key inputs
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		} else if (evt.key.key == SDLK_A) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			// compute yaw/pitch from the camera's current rotation:
			glm::mat4x3 frame = camera->transform->make_parent_from_local();
			glm::vec3 fwd = -glm::normalize(frame[2]); // camera forward in world space

			static float yaw   = std::atan2(fwd.y, fwd.x);
			static float pitch = std::atan2(fwd.z, glm::length(glm::vec2(fwd.x, fwd.y)));

			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);

			//first person camera controls assisted by ChatGPT

			// yaw/pitch for first person POV
			static float yaw = 0.0f;   // left/right
			static float pitch = 0.0f; // up/down

			const float sensitivity = camera->fovy; // keep same scaling as before

			yaw   += -motion.x * sensitivity;
			pitch +=  motion.y * sensitivity;

			// clamp pitch to avoid flipping upside down
			const float max_pitch = glm::radians(179.0f);
			if (pitch >  max_pitch) pitch =  max_pitch;
			const float min_pitch = glm::radians(1.0f);
			if (pitch < min_pitch) pitch = min_pitch;

			// build rotation: yaw around Z-up, then pitch around X-right
			glm::quat qyaw   = glm::angleAxis(yaw,   glm::vec3(0.0f, 0.0f, 1.0f));
			glm::quat qpitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
			camera->transform->rotation = qyaw * qpitch;

			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//check game state
	if (GameOver) return;

	//update PeeTimer
	
	if (!GameStart) PeeTimer -= elapsed;
	else {
		StartTimer -= elapsed;
		if (StartTimer <= 0.0f) GameStart = false;
	}
	if (PeeTimer <= 0.0f) GameOver = true;


	//helper to "reset" scene and go to "next level"
	auto nextLevel = [this](){ 
		//play flush
		if (this->flush_oneshot) this->flush_oneshot->stop();
		this->flush_oneshot = Sound::play_3D(*flush, 0.1f, glm::vec3(0.0f, 0.0f, 0.0f));

		//pick a random position away from toilet
		glm::vec3 new_pos(0.0f);
		auto randf = [](float min, float max) {
			return min + (max - min) * (float(rand()) / float(RAND_MAX));
		};
		while (glm::all(glm::lessThanEqual(glm::abs(new_pos), glm::vec3(MinAwayDistance)))) {
			new_pos = glm::vec3(
				randf(-MaxAwayDistance, MaxAwayDistance),
				randf(-MaxAwayDistance, MaxAwayDistance),
				CameraHeight
			);
		}

		this->camera->transform->position = new_pos;
		PeeTimer = PeeTimerMax;

		//pick position for margit
		glm::vec3 dir = new_pos - glm::vec3(0.0f);
		float len = glm::length(dir);
		float margin = 15.0f;
		float d = randf(margin, len - margin);
		glm::vec3 unit = dir / len;
		//set x,y position SOMEWHERE directly between player and toilet + some offset
		margit_pos = unit * d + glm::vec3(
			randf(-15.0f, 15.0f),
			randf(-15.0f, 15.0f),
			0.0f
		);
		margit_pos.z = CameraHeight;
		
		//update music position
		if (this->margit_loop) {
			this->margit_loop->set_position(margit_pos);
		}

		//update margit position (set underground to be invisible)
		if (this->margit_model) {
			this->margit_model->position = margit_pos + glm::vec3(0.0f, 0.0f, -100.0f);
		}
	}; 

	// check camera position & "collisions"
	glm::vec3 pos = camera->transform->position;
	// camera is close to toilet (origin)
	if (glm::all(glm::lessThanEqual(glm::abs(pos), glm::vec3(10.0f)))) {
		nextLevel();
	}
	if (glm::all(glm::lessThanEqual(glm::abs(pos - margit_pos), glm::vec3(15.0f)))) {
		DeathAnimation = true;
		//make margit face the player, positional changes are updated after
		glm::vec3 dir = camera->transform->position - margit_model->position;
    	dir.z = 0.0f; // flatten to XY plane
		if (glm::length(dir) > 0.001f) {
			dir = glm::normalize(dir);

			// compute yaw angle around Z (vertical)
			float angle = std::atan2(dir.y, dir.x);

			// set Margit's rotation: snap to face player
			margit_model->rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
		}
	}


	//move camera:
	if (!DeathAnimation) {
		//combine inputs into a move:
		constexpr float PlayerSpeed = 45.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];

		// flatten vectors onto XY plane (Z=0):
		glm::vec3 flat_forward = glm::normalize(glm::vec3(frame_forward.x, frame_forward.y, 0.0f));
		glm::vec3 flat_right   = glm::normalize(glm::vec3(frame_right.x,   frame_right.y,   0.0f));

		// apply movement, but keep Z fixed:
		float fixed_height = camera->transform->position.z;
		camera->transform->position += move.x * flat_right + move.y * flat_forward;
		camera->transform->position.z = fixed_height;
	}
	else {
		if (this->margit_model) {
			this->margit_model->position = margit_pos + glm::vec3(0.0f, 0.0f, 10.0f * DeathTimer);
		}
		DeathTimer -= elapsed;
		if (DeathTimer <= 0.0f) {
			DeathAnimation = false;
			GameOver = true;
		}
	}


	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		float ofs = 2.0f / drawable_size.y;
	
		// intro "cutscene"
		if (GameStart) {
			lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
				glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			
			lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
				glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + 0.1f * H + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
			constexpr float H2 = 0.18f;
			lines.draw_text("I NEED TO PEE",
				glm::vec3(-(aspect / 4.0), -(aspect / 4.0), 0.0),
				glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text("I NEED TO PEE",
				glm::vec3(-(aspect / 4.0) + ofs, -(aspect / 4.0) + ofs, 0.0),
				glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}

		// main timer
		if (!GameStart && !GameOver && !DeathAnimation) {
			constexpr float H2 = 0.18f;
			int TimerText = (int)(std::ceil(PeeTimer));
			lines.draw_text(std::to_string(TimerText),
				glm::vec3(-(aspect / 5.0), -(aspect / 4.0), 0.0),
				glm::vec3(2.0 * H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text(std::to_string(TimerText),
				glm::vec3(-(aspect / 5.0) + ofs, -(aspect / 4.0) + ofs, 0.0),
				glm::vec3(2.0 * H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}

		// gameover screen
		if (GameOver) {
			constexpr float H2 = 0.18f;
			lines.draw_text("peed myself :()",
				glm::vec3(-(aspect / 5.0), -(aspect / 4.0), 0.0),
				glm::vec3(2.0 * H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text("peed myself :()",
				glm::vec3(-(aspect / 5.0) + ofs, -(aspect / 4.0) + ofs, 0.0),
				glm::vec3(2.0 * H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
	}
	GL_ERRORS();
}

//TODO replace with helper to decide audio positioning

//note: may need to check old code to see how to get skeleton/model pointers
glm::vec3 PlayMode::get_leg_tip_position() {
	// //the vertex position here was read from the model in blender:
	// return lower_leg->make_world_from_local() * glm::vec4(-1.26137f, -11.861f, 0.0f, 1.0f);

	return glm::vec3(0.0f, 0.0f, 0.0f);
}

