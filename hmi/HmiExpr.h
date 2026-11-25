#pragma once

//-----------------------------------------------------------//
// HMI computed tags (kind "expr"): value conversion, expression
// validation and evaluation.
//
// QtCore only, so the host-side test in test/hmi_expr can build it
// without the rest of the server. The rules match the frontend rule
// engine (swr_web js/whmi-rules.js, js/whmi-runtime.js WHMI.raw); the
// shared test data in swr_web/referencs/hmi-testdata-*.json is the
// reference when the two disagree.
//-----------------------------------------------------------//

#include <QHash>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>

#include <functional>

namespace Hmi {

// Limits checked when a tag is saved or imported.
constexpr int kMaxTags      = 1000;   // all HMI tags, every kind
constexpr int kExprMaxNodes = 64;     // nodes in one expression
constexpr int kExprMaxRefs  = 32;     // distinct tags one expression refers to
constexpr int kExprMaxDepth = 8;      // expr -> expr chain length
constexpr int kBitMax       = 15;     // bitAny / bitNone bit numbers
constexpr int kDecimalsMax  = 6;      // decimals of a number expr / param tag
constexpr int kParamTextMax = 64;     // length of a string param value (UTF-16 units)

//-----------------------------------------------------------//
// Raw word -> display value
//-----------------------------------------------------------//
// Decimal places implied by a scale: 0.1 -> 1, 0.01 -> 2, 1 or more -> 0.
int scaleDigits(double scale);

// Round to the given decimal places, so 2472 * 0.1 compares equal to 247.2.
double roundTo(double value, int digits);

// decimals of a number expr tag: null or absent -> -1 (no rounding),
// an integer 0..kDecimalsMax -> that value. false for anything else.
bool decimalsFromJson(const QJsonValue &v, int &out);

// raw: null or an unsigned word (0–65535). Coils and discrete inputs give 0/1
// and ignore the scale. Signed registers read the word as 16-bit two's complement.
// Returns null for a null raw.
QJsonValue toDisplay(const QJsonValue &raw, bool isBool, bool isSigned, double scale);

// Register min/max are in register-scale units; this converts one to the tag
// scale. Returns false when the register has no limit (±1.797e308 means none).
bool inheritLimit(double registerLimit, double registerScale, double tagScale,
                  double &out);

//-----------------------------------------------------------//
// Values and quality
//-----------------------------------------------------------//
enum class Quality { Good = 0, Normal = 1, Bad = 2 };   // worse is larger

QString qualityToString(Quality q);
// "good" / "normal" / "bad" / "disabled" (disabled counts as bad).
// Returns false for anything else.
bool qualityFromString(const QString &s, Quality &out);

enum class ValueType { Bool, Number, String };
bool valueTypeFromString(const QString &s, ValueType &out);
QString valueTypeToString(ValueType t);

// Frontend reading rules
double  toNumber(const QJsonValue &v);     // NaN when not readable as a number
bool    isTruthy(const QJsonValue &v);     // 0, false, "", "0", "false" are false
QString toText(const QJsonValue &v);       // JavaScript String(v) for scalars

//-----------------------------------------------------------//
// Validation (saving, importing, the eval API)
//-----------------------------------------------------------//
struct ExprInfo {
    QSet<QString> refs;   // tag ids the expression refers to
    int           nodes = 0;
};

// Checks node shapes, operator names and argument counts, bit lists and the
// size limits. On failure, error says what is wrong and path points at the
// node ("" = the root, "args[1].args[0]" = a nested node).
bool validateExpr(const QJsonValue &expr, ExprInfo &info,
                  QString &error, QString &path);

// exprRefs: every expr tag -> the tag ids its expression refers to.
// Rejects circular references and expr -> expr chains longer than
// kExprMaxDepth. Refs to tags that are not expr tags end a chain.
bool checkGraph(const QHash<QString, QSet<QString>> &exprRefs, QString &error);

//-----------------------------------------------------------//
// Evaluation
//-----------------------------------------------------------//
struct Sample {
    QJsonValue value;            // null when q is Bad
    Quality    q = Quality::Bad;
};

// What the evaluator knows about one tag id.
struct TagInput {
    enum class Kind { Missing, Register, Expr, Param };
    Kind kind = Kind::Missing;

    // Register: the raw word and its quality ("good"/"normal"/"bad"/"disabled")
    QJsonValue raw;
    Quality    rawQ     = Quality::Bad;
    bool       isBool   = false;
    bool       isSigned = false;
    double     scale    = 1.0;

    // Expr: the expression, or a fixed value given by the caller (eval API inputs).
    // Param: the value is always fixedSample (the stored value or an eval input).
    ValueType  valueType = ValueType::Bool;
    int        decimals  = -1;    // number results only; -1 = no rounding
    QJsonValue expr;
    bool       fixed = false;
    Sample     fixedSample;
};

// The value each tag actually had during one evaluation.
struct UsedInput {
    TagInput::Kind kind = TagInput::Kind::Missing;
    QJsonValue     raw;          // register tags only
    Sample         sample;
};

using TagLookup = std::function<TagInput(const QString &id)>;

//-----------------------------------------------------------//
// Setting (param) tags: a value the user sets on the HMI, kept on the
// device. Types are strict: number = JSON number, bool = true/false,
// string = text of at most kParamTextMax characters.
//-----------------------------------------------------------//
struct ParamDef {
    ValueType  vt = ValueType::Number;
    QJsonValue initial;            // already rounded to decimals
    bool       hasMin = false;     // number tags only
    double     min    = 0.0;
    bool       hasMax = false;
    double     max    = 0.0;
    int        decimals = -1;      // number tags only; -1 = no rounding
};

enum class ParamStatus { Ok, BadField, BadRule };   // BadField -> 400, BadRule -> 422

// Reads a definition. Absent fields are Undefined. min / max / decimals
// of a bool or string tag are ignored.
ParamStatus parseParamDef(const QJsonValue &valueType, const QJsonValue &initial,
                          const QJsonValue &min, const QJsonValue &max,
                          const QJsonValue &decimals, ParamDef &out, QString &error);

// Checks a value against a definition: type, then (number) rounding to
// decimals with -0 made 0, then the range. On a range failure outOfRange
// is set and out holds the rounded value. what names the value in errors.
bool acceptParamValue(const ParamDef &def, const QJsonValue &v, const QString &what,
                      QJsonValue &out, QString &error, bool &outOfRange);

// The current value after a definition change.
enum class ParamCarry { Kept, TypeChanged, Rounded, Clamped, Reset };
QJsonValue carryParamValue(const ParamDef &def, bool sameType, const QJsonValue &oldValue,
                           ParamCarry &how);

// Storage as a JSON scalar ("85.5", "true", "\"주간\""); Undefined when unreadable.
QString    paramToText(const QJsonValue &v);
QJsonValue paramFromText(const QString &text);
// For logs: strings quoted, the rest as toText.
QString    paramLogText(const QJsonValue &v);

class Evaluator {
public:
    explicit Evaluator(TagLookup lookup);

    // Value of a tag (register: converted raw; expr: its result). Each tag is
    // evaluated once per Evaluator.
    Sample evalTag(const QString &id);

    // Value of a free expression (the eval API), converted to valueType.
    Sample evalExpr(const QJsonValue &expr, ValueType valueType, int decimals = -1);

    // Every tag the evaluations so far touched, directly or through expr tags.
    const QMap<QString, UsedInput> &used() const { return m_used; }

private:
    Sample evalNode(const QJsonValue &node);
    Sample finish(const Sample &s, ValueType valueType, int decimals) const;

    TagLookup                m_lookup;
    QMap<QString, UsedInput> m_used;
    QSet<QString>            m_inProgress;
};

} // namespace Hmi
