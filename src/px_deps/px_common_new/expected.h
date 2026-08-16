//
// Created by RGAA on 26/03/2025.
//

#ifndef PX_EXPECTED_H
#define PX_EXPECTED_H

#include <expected>
#include <string>

namespace px
{

    template<class T, class E>
    using Result = std::expected<T, E>;

    template<typename T>
    using ResultStrErr = Result<T, std::string>;

    template<class T>
    static Result<T, std::string> Err(const std::string& err) {
        return std::unexpected(err);
    }

    template<class T>
    static Result<T, int> ErrInt(int err) {
        return std::unexpected(err);
    }


#define TRError std::unexpected
#define TcErr std::unexpected

}

#endif //PX_EXPECTED_H
