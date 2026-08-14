#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "gl_function.h"

namespace tc
{

	class Director {
	public:

		static Director* Instance() {
            static Director director;
            return &director;
        }

		Director();
		~Director();

		void Init(GLFunctions* fk, int width, int height);

		glm::mat4 GetProjection();
		glm::mat4 GetView();

		GLFunctions* Funcs();

	private:

		glm::mat4 projection;
		glm::mat4 view;

		float width = 0.0f;
		float height = 0.0f;

		GLFunctions* funcs = nullptr;

	};

}

#if defined(WIN32) || defined(PC_PLATFORM)
#define GL_FUNC Director::Instance()->Funcs()->core_->
#endif

#ifdef ANDROID
#define GL_FUNC
#endif