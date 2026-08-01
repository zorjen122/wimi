#pragma once

#include "Const.h"

#include <string>

namespace wimi
{

struct VerificationCodeIssue {
    int error{ErrorCodes::Success};
    bool issued{false};
    std::string developmentCode;
};

class VerificationService : public Singleton<VerificationService>
{
    friend class Singleton<VerificationService>;

  public:
    VerificationCodeIssue RequestEmailCode(const std::string& email);
    bool VerifyAndConsume(const std::string& email, const std::string& code);

    static std::string NormalizeEmail(const std::string& email);
    static bool IsValidEmail(const std::string& email);

  private:
    VerificationService();

    std::string GenerateCode() const;
    bool SendEmail(const std::string& recipient, const std::string& code) const;

    int codeTtlSeconds_{180};
    int resendCooldownSeconds_{60};
    int maxAttempts_{5};
    bool emailEnabled_{false};
    bool exposeCodeInResponse_{false};
    std::string smtpUrl_{"smtps://smtp.qq.com:465"};
    std::string emailUser_;
    std::string emailPassword_;
};

} // namespace wimi
