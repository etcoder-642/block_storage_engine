#ifndef RESULT_H
#define RESULT_H

#include <iostream>
#include <string>
#include <vector>
#include <variant>
#include <optional>

#include "error.h"

namespace bse
{
    template <typename T>
    class Result
    {
    private:
        std::variant<T, BSError> data;

        Result() = default;

    public:
        // Factory methods
        // Factory methods
        static Result<T> Ok(T value)
        {
            Result r;
            r.data = std::move(value);
            return r;
        }

        static Result<T> Err(ErrorCode code, std::string message, std::string suggestion = "")
        {
            Result r;
            r.data = BSError(code, message, suggestion);
            return r;
        }

        static Result<T> Err(BSError err)
        {
            Result r;
            r.data = err;
            return r;
        }

        // State Checks
        bool isOk() const
        {
            return std::holds_alternative<T>(data);
        }

        bool isErr() const
        {
            return std::holds_alternative<BSError>(data);
        }

        // Extraction
        T unwrap()
        {
            return std::get<T>(data);
        }

        BSError unwrapErr()
        {
            return std::get<BSError>(data);
        }

        ErrorCode getErrCode()
        {
            return std::get<BSError>(data).code;
        }

        std::string getErrMessage()
        {
            return std::get<BSError>(data).message;
        }

        std::string getErrSuggestion()
        {
            return std::get<BSError>(data).suggestion;
        }

        void printStackTrace()
        {
            data.value().printStackTrace();
        }
    };

    // Void Specialization
    template <>
    class Result<void>
    {
    private:
        std::optional<BSError> err_;

        Result() = default;

    public:
        // Factory methods
        // Factory methods
        static Result<void> Ok()
        {
            return Result{};
        }

        static Result<void> Err(ErrorCode code, std::string message, std::string suggestion = "")
        {
            Result r;
            r.err_ = BSError(code, message, suggestion);
            return r;
        }

        static Result<void> Err(BSError err)
        {
            Result r;
            r.err_ = err;
            return r;
        }

        // State Checks
        bool isOk() const
        {
            return !err_.has_value();
        }

        bool isErr() const
        {
            return err_.has_value();
        }

        // Extraction
        BSError unwrapErr()
        {
            return err_.value();
        }

        ErrorCode getErrCode()
        {
            return err_.value().code;
        }

        std::string getErrMessage()
        {
            return err_.value().message;
        }

        std::string getErrSuggestion()
        {
            return err_.value().suggestion;
        }

        void printStackTrace()
        {
            err_.value().printStackTrace();
        }
    };
}

#endif