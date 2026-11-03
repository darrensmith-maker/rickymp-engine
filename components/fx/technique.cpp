#include "technique.hpp"

#include "lexer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace
{
    using namespace Fx::Lexer;

    template <class T>
    bool is(const Token& token)
    {
        return std::holds_alternative<T>(token);
    }

    template <class T>
    const T& as(const Token& token)
    {
        return std::get<T>(token);
    }

    template <class T>
    void expect(Lexer& lexer, const char* what)
    {
        Token token = lexer.next();
        if (!is<T>(token))
        {
            std::ostringstream message;
            message << "expected " << what;
            lexer.error(message.str());
        }
    }

    std::string literal(Lexer& lexer)
    {
        Token token = lexer.next();
        if (!is<Literal>(token))
            lexer.error("expected identifier");
        return std::string(as<Literal>(token).mValue);
    }

    std::string stringValue(Lexer& lexer)
    {
        Token token = lexer.next();
        if (is<String>(token))
            return std::string(as<String>(token).mValue);
        if (is<Literal>(token))
            return std::string(as<Literal>(token).mValue);
        lexer.error("expected string or identifier");
    }

    float numberValue(Lexer& lexer)
    {
        Token token = lexer.next();
        if (is<Float>(token))
            return as<Float>(token).mValue;
        if (is<Integer>(token))
            return static_cast<float>(as<Integer>(token).mValue);
        lexer.error("expected number");
    }

    int integerValue(Lexer& lexer)
    {
        Token token = lexer.next();
        if (is<Integer>(token))
            return as<Integer>(token).mValue;
        lexer.error("expected integer");
    }

    bool boolValue(Lexer& lexer)
    {
        Token token = lexer.next();
        if (is<True>(token))
            return true;
        if (is<False>(token))
            return false;
        if (is<Boolean>(token))
            return as<Boolean>(token).mValue;
        lexer.error("expected boolean");
    }

    std::array<float, 4> vectorValue(Lexer& lexer, int components)
    {
        Token token = lexer.next();
        const bool vectorToken = (components == 2 && is<Vec2>(token)) || (components == 3 && is<Vec3>(token))
            || (components == 4 && is<Vec4>(token));
        if (!vectorToken)
            lexer.error("expected vector constructor");
        expect<Open_Parenthesis>(lexer, "'('");

        std::array<float, 4> value{{0.f, 0.f, 0.f, 0.f}};
        for (int i = 0; i < components; ++i)
        {
            value[static_cast<std::size_t>(i)] = numberValue(lexer);
            if (i + 1 < components)
                expect<Comma>(lexer, "','");
        }
        expect<Close_Parenthesis>(lexer, "')'");
        return value;
    }

    void expectAssignment(Lexer& lexer)
    {
        expect<Equal>(lexer, "'='");
    }

    void expectTerminator(Lexer& lexer)
    {
        expect<SemiColon>(lexer, "';'");
    }

    std::vector<std::string> literalList(Lexer& lexer)
    {
        std::vector<std::string> result;
        while (true)
        {
            result.push_back(literal(lexer));
            Token next = lexer.next();
            if (is<SemiColon>(next))
                break;
            if (!is<Comma>(next))
                lexer.error("expected ',' or ';' in list");
        }
        return result;
    }

    Fx::FlagsType flagFor(std::string name)
    {
        // Case-insensitive, as OpenMW 0.51 (ciEqual): effects write "Disable_AboveWater", "Disable_SunGlare"...
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "disable_interiors")
            return Fx::Technique::Flag_Disable_Interiors;
        if (name == "disable_exteriors")
            return Fx::Technique::Flag_Disable_Exteriors;
        if (name == "disable_underwater")
            return Fx::Technique::Flag_Disable_Underwater;
        if (name == "disable_abovewater")
            return Fx::Technique::Flag_Disable_Abovewater;
        if (name == "disable_sunglare")
            return Fx::Technique::Flag_Disable_SunGlare;
        if (name == "hidden")
            return Fx::Technique::Flag_Hidden;
        return 0;
    }

    std::array<float, 4> defaultMinimum(Fx::UniformType type)
    {
        const float low = (type == Fx::UniformType::Bool) ? 0.f : -std::numeric_limits<float>::max();
        return {{low, low, low, low}};
    }

    std::array<float, 4> defaultMaximum(Fx::UniformType type)
    {
        const float high = (type == Fx::UniformType::Bool) ? 1.f : std::numeric_limits<float>::max();
        return {{high, high, high, high}};
    }

    int componentsFor(Fx::UniformType type)
    {
        switch (type)
        {
            case Fx::UniformType::Vec2: return 2;
            case Fx::UniformType::Vec3: return 3;
            case Fx::UniformType::Vec4: return 4;
            default: return 1;
        }
    }

    std::array<float, 4> parseUniformValue(Lexer& lexer, Fx::UniformType type)
    {
        std::array<float, 4> value{{0.f, 0.f, 0.f, 0.f}};
        if (type == Fx::UniformType::Bool)
        {
            value[0] = boolValue(lexer) ? 1.f : 0.f;
            return value;
        }
        if (type == Fx::UniformType::Int)
        {
            value[0] = static_cast<float>(integerValue(lexer));
            return value;
        }
        if (type == Fx::UniformType::Float)
        {
            value[0] = numberValue(lexer);
            return value;
        }
        return vectorValue(lexer, componentsFor(type));
    }

    void parseUniformBlock(const std::string& block, Fx::UniformSpec& uniform)
    {
        Lexer lexer(block);
        uniform.mMin = defaultMinimum(uniform.mType);
        uniform.mMax = defaultMaximum(uniform.mType);

        while (true)
        {
            Token token = lexer.next();
            if (is<Eof>(token))
                break;
            if (!is<Literal>(token))
                lexer.error("expected uniform property");

            const std::string key(as<Literal>(token).mValue);
            expectAssignment(lexer);

            if (key == "default")
                uniform.mDefault = parseUniformValue(lexer, uniform.mType);
            else if (key == "min")
                uniform.mMin = parseUniformValue(lexer, uniform.mType);
            else if (key == "max")
                uniform.mMax = parseUniformValue(lexer, uniform.mType);
            else if (key == "step")
                uniform.mStep = numberValue(lexer);
            else if (key == "display_name")
                uniform.mDisplayName = stringValue(lexer);
            else if (key == "description")
                uniform.mDescription = stringValue(lexer);
            else if (key == "header")
                uniform.mHeader = stringValue(lexer);
            else if (key == "static")
                uniform.mStatic = boolValue(lexer);
            else if (key == "widget_type")
            {
                Token widget = lexer.next();
                if (!is<Literal>(widget) || as<Literal>(widget).mValue != "choice")
                    lexer.error("unsupported uniform widget_type");
                expect<Open_Parenthesis>(lexer, "'('");
                while (true)
                {
                    Token next = lexer.peek();
                    if (is<Close_Parenthesis>(next))
                    {
                        lexer.next();
                        break;
                    }
                    Fx::UniformChoice choice;
                    choice.mLabel = stringValue(lexer);
                    expectAssignment(lexer);
                    choice.mValue = parseUniformValue(lexer, uniform.mType);
                    uniform.mChoices.push_back(std::move(choice));
                    next = lexer.next();
                    if (is<Close_Parenthesis>(next))
                        break;
                    if (!is<Comma>(next))
                        lexer.error("expected ',' or ')' in choice widget");
                }
            }
            else
            {
                // Accept metadata that this C++17 backport does not need yet by
                // consuming one simple scalar/string value. Complex metadata is
                // rejected so a malformed shader cannot silently desynchronise
                // the lexer.
                Token ignored = lexer.next();
                if (is<Vec2>(ignored) || is<Vec3>(ignored) || is<Vec4>(ignored)
                    || is<Open_Parenthesis>(ignored) || is<Open_bracket>(ignored))
                    lexer.error("unsupported complex uniform metadata property");
            }
            expectTerminator(lexer);
        }
    }

    void parseRenderTargetBlock(const std::string& block, Fx::RenderTargetSpec& target)
    {
        Lexer lexer(block);
        while (true)
        {
            Token token = lexer.next();
            if (is<Eof>(token))
                break;
            if (!is<Literal>(token))
                lexer.error("expected render-target property");
            const std::string key(as<Literal>(token).mValue);
            expectAssignment(lexer);

            if (key == "width_ratio")
                target.mWidthRatio = numberValue(lexer);
            else if (key == "height_ratio")
                target.mHeightRatio = numberValue(lexer);
            else if (key == "width")
                target.mWidth = integerValue(lexer);
            else if (key == "height")
                target.mHeight = integerValue(lexer);
            else if (key == "internal_format")
                target.mInternalFormat = stringValue(lexer);
            else if (key == "source_format")
                target.mSourceFormat = stringValue(lexer);
            else if (key == "source_type")
                target.mSourceType = stringValue(lexer);
            else if (key == "min_filter")
                target.mMinFilter = stringValue(lexer);
            else if (key == "mag_filter")
                target.mMagFilter = stringValue(lexer);
            else if (key == "wrap_s")
                target.mWrapS = stringValue(lexer);
            else if (key == "wrap_t")
                target.mWrapT = stringValue(lexer);
            else if (key == "mipmaps")
                target.mMipMaps = boolValue(lexer);
            else if (key == "clear_color")
                target.mClearColor = vectorValue(lexer, 4);
            else
                (void)stringValue(lexer);

            expectTerminator(lexer);
        }
    }

    void parsePassHeader(Lexer& lexer, Fx::PassSpec& pass)
    {
        Token next = lexer.peek();
        if (!is<Open_Parenthesis>(next))
            return;
        lexer.next();

        while (true)
        {
            Token token = lexer.next();
            if (is<Close_Parenthesis>(token))
                break;
            if (!is<Literal>(token))
                lexer.error("expected pass argument");
            const std::string key(as<Literal>(token).mValue);
            expectAssignment(lexer);
            const std::string value = literal(lexer);

            if (key == "target")
                pass.mTarget = value;
            else if (key.size() == 3 && key[0] == 'r' && key[1] == 't' && key[2] >= '1' && key[2] <= '3')
                pass.mInputs[static_cast<std::size_t>(key[2] - '1')] = value;

            Token delimiter = lexer.next();
            if (is<Close_Parenthesis>(delimiter))
                break;
            if (!is<Comma>(delimiter))
                lexer.error("expected ',' or ')' in pass arguments");
        }
    }

    void parseTechniqueBlock(const std::string& block, Fx::Technique& technique,
        std::vector<std::string>& passOrder, std::string& author, std::string& description, std::string& version,
        Fx::FlagsType& flags, bool& passNormals, bool& hdr, bool& passLights)
    {
        Lexer lexer(block);
        while (true)
        {
            Token token = lexer.next();
            if (is<Eof>(token))
                break;
            if (!is<Literal>(token))
                lexer.error("expected technique property");
            const std::string key(as<Literal>(token).mValue);
            expectAssignment(lexer);

            if (key == "passes")
            {
                passOrder = literalList(lexer);
                continue;
            }
            if (key == "author")
                author = stringValue(lexer);
            else if (key == "description")
                description = stringValue(lexer);
            else if (key == "version")
                version = stringValue(lexer);
            else if (key == "pass_normals")
                passNormals = boolValue(lexer);
            else if (key == "hdr")
                hdr = boolValue(lexer);
            else if (key == "pass_lights")
                passLights = boolValue(lexer);
            else if (key == "flags")
            {
                while (true)
                {
                    const std::string flag = literal(lexer);
                    const Fx::FlagsType bit = flagFor(flag);
                    if (bit == 0)
                        lexer.error("unrecognized technique flag");
                    flags |= bit;
                    Token delimiter = lexer.next();
                    if (is<SemiColon>(delimiter))
                        break;
                    if (!is<VBar>(delimiter) && !is<Comma>(delimiter))
                        lexer.error("expected '|', ',' or ';' in flags");
                }
                continue;
            }
            else
                (void)stringValue(lexer);

            expectTerminator(lexer);
        }
        (void)technique;
    }

    Fx::UniformType uniformType(const Token& token)
    {
        if (is<Uniform_Bool>(token)) return Fx::UniformType::Bool;
        if (is<Uniform_Int>(token)) return Fx::UniformType::Int;
        if (is<Uniform_Float>(token)) return Fx::UniformType::Float;
        if (is<Uniform_Vec2>(token)) return Fx::UniformType::Vec2;
        if (is<Uniform_Vec3>(token)) return Fx::UniformType::Vec3;
        if (is<Uniform_Vec4>(token)) return Fx::UniformType::Vec4;
        throw std::runtime_error("not a uniform token");
    }

    bool isUniformToken(const Token& token)
    {
        return is<Uniform_Bool>(token) || is<Uniform_Int>(token) || is<Uniform_Float>(token)
            || is<Uniform_Vec2>(token) || is<Uniform_Vec3>(token) || is<Uniform_Vec4>(token);
    }
}

namespace Fx
{
    int UniformSpec::components() const
    {
        return componentsFor(mType);
    }

    std::string UniformSpec::glslDeclaration() const
    {
        const char* typeName = "float";
        switch (mType)
        {
            case UniformType::Bool: typeName = "bool"; break;
            case UniformType::Int: typeName = "int"; break;
            case UniformType::Float: typeName = "float"; break;
            case UniformType::Vec2: typeName = "vec2"; break;
            case UniformType::Vec3: typeName = "vec3"; break;
            case UniformType::Vec4: typeName = "vec4"; break;
        }
        return std::string("uniform ") + typeName + " " + mName + ";";
    }

    std::pair<int, int> RenderTargetSpec::sizeFor(int width, int height) const
    {
        int resultWidth = mWidth > 0 ? mWidth : std::max(1, static_cast<int>(std::lround(width * mWidthRatio)));
        int resultHeight = mHeight > 0 ? mHeight : std::max(1, static_cast<int>(std::lround(height * mHeightRatio)));
        return std::make_pair(resultWidth, resultHeight);
    }

    void Technique::clear()
    {
        mShared.clear();
        mUniforms.clear();
        mRenderTargets.clear();
        mPasses.clear();
        mOrderedPasses.clear();
        mPassOrder.clear();
        mAuthor.clear();
        mDescription.clear();
        mVersion.clear();
        mFlags = 0;
        mPassNormals = false;
        mHDR = false;
        mPassLights = false;
    }

    const RenderTargetSpec* Technique::findRenderTarget(const std::string& name) const
    {
        const auto it = std::find_if(mRenderTargets.begin(), mRenderTargets.end(),
            [&](const RenderTargetSpec& target) { return target.mName == name; });
        return it == mRenderTargets.end() ? nullptr : &*it;
    }

    bool Technique::parse(const std::string& source, std::string* error)
    {
        clear();
        try
        {
            Lexer::Lexer lexer(source);
            while (true)
            {
                Lexer::Token token = lexer.next();
                if (is<Lexer::Eof>(token))
                    break;

                if (isUniformToken(token))
                {
                    UniformSpec uniform;
                    uniform.mType = uniformType(token);
                    uniform.mName = literal(lexer);
                    expect<Lexer::Open_bracket>(lexer, "'{'");
                    const auto block = lexer.jump();
                    if (!block)
                        lexer.error("unterminated uniform block");
                    parseUniformBlock(std::string(*block), uniform);
                    expect<Lexer::Close_bracket>(lexer, "'}'");
                    mUniforms.push_back(uniform);
                    continue;
                }

                if (is<Lexer::Shared>(token))
                {
                    expect<Lexer::Open_bracket>(lexer, "'{'");
                    const auto block = lexer.jump();
                    if (!block)
                        lexer.error("unterminated shared block");
                    mShared = std::string(*block);
                    expect<Lexer::Close_bracket>(lexer, "'}'");
                    continue;
                }

                if (is<Lexer::Render_Target>(token))
                {
                    RenderTargetSpec target;
                    target.mName = literal(lexer);
                    expect<Lexer::Open_bracket>(lexer, "'{'");
                    const auto block = lexer.jump();
                    if (!block)
                        lexer.error("unterminated render_target block");
                    parseRenderTargetBlock(std::string(*block), target);
                    expect<Lexer::Close_bracket>(lexer, "'}'");
                    mRenderTargets.push_back(target);
                    continue;
                }

                if (is<Lexer::Fragment>(token))
                {
                    PassSpec pass;
                    if (is<Lexer::Literal>(lexer.peek()))
                        pass.mName = literal(lexer);
                    else
                        pass.mName = "main";
                    parsePassHeader(lexer, pass);
                    expect<Lexer::Open_bracket>(lexer, "'{'");
                    const auto block = lexer.jump();
                    if (!block)
                        lexer.error("unterminated fragment block");
                    pass.mSource = std::string(*block);
                    expect<Lexer::Close_bracket>(lexer, "'}'");
                    mPasses.push_back(pass);
                    continue;
                }

                if (is<Lexer::Technique>(token))
                {
                    expect<Lexer::Open_bracket>(lexer, "'{'");
                    const auto block = lexer.jump();
                    if (!block)
                        lexer.error("unterminated technique block");
                    parseTechniqueBlock(std::string(*block), *this, mPassOrder, mAuthor, mDescription, mVersion,
                        mFlags, mPassNormals, mHDR, mPassLights);
                    expect<Lexer::Close_bracket>(lexer, "'}'");
                    continue;
                }

                // Vertex/compute/sampler blocks belong to later renderer migration layers.
                // Fail loudly instead of silently accepting an effect that R3 cannot execute correctly.
                lexer.error("R3 supports shared/uniform/render_target/fragment/technique blocks only");
            }

            if (mPasses.empty())
                throw std::runtime_error("effect has no fragment passes");

            if (mPassOrder.empty())
            {
                for (const PassSpec& pass : mPasses)
                    mPassOrder.push_back(pass.mName);
            }

            for (const std::string& passName : mPassOrder)
            {
                const auto it = std::find_if(mPasses.begin(), mPasses.end(),
                    [&](const PassSpec& pass) { return pass.mName == passName; });
                if (it == mPasses.end())
                    throw std::runtime_error(std::string("technique references missing pass '") + passName + "'");
                mOrderedPasses.push_back(*it);
            }

            for (const PassSpec& pass : mOrderedPasses)
            {
                if (!pass.mTarget.empty() && !findRenderTarget(pass.mTarget))
                    throw std::runtime_error(std::string("pass references missing render target '") + pass.mTarget + "'");
                for (const std::string& input : pass.mInputs)
                {
                    if (!input.empty() && !findRenderTarget(input))
                        throw std::runtime_error(std::string("pass references missing input render target '") + input + "'");
                }
            }

            return true;
        }
        catch (const std::exception& e)
        {
            if (error)
                *error = e.what();
            clear();
            return false;
        }
    }
}
