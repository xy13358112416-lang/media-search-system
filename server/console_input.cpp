#include "console_input.h"
#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace console_input {
bool isTerminal()
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

namespace {
#ifdef Q_OS_WIN
HANDLE protectedHandle = INVALID_HANDLE_VALUE;
DWORD protectedMode = 0;
BOOL WINAPI restoreOnInterrupt(DWORD)
{
    if (protectedHandle != INVALID_HANDLE_VALUE) SetConsoleMode(protectedHandle, protectedMode);
    return FALSE; // Let the normal console handler finish cancellation.
}
#endif
class EchoGuard {
public:
    explicit EchoGuard(bool hide) : required_(hide)
    {
        if (!hide) return;
#ifdef Q_OS_WIN
        handle_ = GetStdHandle(STD_INPUT_HANDLE);
        if (!GetConsoleMode(handle_, &mode_)) return;
        protectedMode = mode_;
        protectedHandle = handle_;
        handler_ = SetConsoleCtrlHandler(restoreOnInterrupt, TRUE);
        changed_ = SetConsoleMode(handle_, mode_ & ~ENABLE_ECHO_INPUT);
#else
        if (tcgetattr(fileno(stdin), &mode_) != 0) return;
        auto hidden = mode_;
        hidden.c_lflag &= ~ECHO;
        changed_ = tcsetattr(fileno(stdin), TCSANOW, &hidden) == 0;
#endif
    }
    ~EchoGuard()
    {
#ifdef Q_OS_WIN
        if (changed_) SetConsoleMode(handle_, mode_);
        if (handler_) SetConsoleCtrlHandler(restoreOnInterrupt, FALSE);
        protectedHandle = INVALID_HANDLE_VALUE;
#else
        if (changed_) tcsetattr(fileno(stdin), TCSANOW, &mode_);
#endif
    }
    bool ready() const { return !required_ || changed_; }
private:
    bool required_ = false;
    bool changed_ = false;
#ifdef Q_OS_WIN
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD mode_ = 0;
    bool handler_ = false;
#else
    termios mode_{};
#endif
};
}

std::optional<QString> readLine(QTextStream& input, QTextStream& output,
                                const QString& prompt, bool secret, QString& error)
{
    const bool hidden = secret && isTerminal();
    EchoGuard echo(hidden);
    if (!echo.ready()) {
        error = QStringLiteral("无法隐藏密码输入，已取消本次操作。");
        return std::nullopt;
    }
    output << prompt << Qt::flush;
    const auto value = input.readLine();
    if (hidden) output << Qt::endl;
    if (value.isNull()) {
        error = QStringLiteral("输入已结束，账号未创建。");
        return std::nullopt;
    }
    return value;
}
}
