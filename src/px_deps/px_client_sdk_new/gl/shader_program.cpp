#include "shader_program.h"

#include <iostream>

namespace tc
{

	std::shared_ptr<ShaderProgram> ShaderProgram::Make(const GLFunctions* fk, const std::string& vertex, const std::string& fragment) {
		return std::make_shared<ShaderProgram>(fk, vertex, fragment);
	}

	ShaderProgram::ShaderProgram(const GLFunctions* fk, const std::string& vertex, const std::string& fragment) {
		this->fk_ = const_cast<GLFunctions*>(fk);

		GLuint vertex_shader = GL_FUNC glCreateShader(GL_VERTEX_SHADER);
		char const* vertex_source = vertex.c_str();
		GL_FUNC glShaderSource(vertex_shader, 1, &vertex_source, NULL);
		GL_FUNC glCompileShader(vertex_shader);
		CheckShaderError(vertex_shader, "vertex");

		GLuint fragment_shader = GL_FUNC glCreateShader(GL_FRAGMENT_SHADER);
		char const* fragment_source = fragment.c_str();
		GL_FUNC glShaderSource(fragment_shader, 1, &fragment_source, NULL);

		GL_FUNC glCompileShader(fragment_shader);
		CheckShaderError(fragment_shader, "fragment");

		program_id_ = GL_FUNC glCreateProgram();

		GL_FUNC glAttachShader(program_id_, vertex_shader);
		GL_FUNC glAttachShader(program_id_, fragment_shader);

		GL_FUNC glLinkProgram(program_id_);
		CheckShaderError(program_id_, "program");
	}

	ShaderProgram::~ShaderProgram() = default;

	void ShaderProgram::CheckShaderError(GLuint id, const std::string& type) {
		int check_flag;
		char check_info[1024];
		if (type != "program") {
			GL_FUNC glGetShaderiv(id, GL_COMPILE_STATUS, &check_flag);
			if (!check_flag) {
				GL_FUNC glGetShaderInfoLog(id, 1024, NULL, check_info);
				std::cout << type << " error:" << check_info << std::endl;;
			}
		}
		else {
			GL_FUNC glGetShaderiv(id, GL_LINK_STATUS, &check_flag);
			if (!check_flag) {
				GL_FUNC glGetProgramInfoLog(id, 1024, NULL, check_info);
				std::cout << type << " error:" << check_info << std::endl;
			}
		}
	}

	int ShaderProgram::GetProgramId() {
		return program_id_;
	}

	void ShaderProgram::Active() {
		GL_FUNC glUseProgram(program_id_);
	}
}