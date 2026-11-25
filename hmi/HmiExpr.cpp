#include "HmiExpr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Hmi {

//-----------------------------------------------------------//
// Raw word -> display value
//-----------------------------------------------------------//
int scaleDigits(double scale)
{
    if (!(scale > 0.0))
        return 0;
    // Math.round(Math.log10(scale)) in the frontend
    const int rounded = static_cast<int>(std::floor(std::log10(scale) + 0.5));
    return rounded < 0 ? -rounded : 0;
}

double roundTo(double value, int digits)
{
    if (!std::isfinite(value))
        return value;
    return QString::number(value, 'f', digits).toDouble();
}

bool decimalsFromJson(const QJsonValue &v, int &out)
{
    if (v.isUndefined() || v.isNull()) {
        out = -1;
        return true;
    }
    const double d = v.toDouble(-1.0);
    if (!v.isDouble() || d != std::floor(d) || d < 0 || d > kDecimalsMax)
        return false;
    out = static_cast<int>(d);
    return true;
}

QJsonValue toDisplay(const QJsonValue &raw, bool isBool, bool isSigned, double scale)
{
    if (!raw.isDouble())
        return QJsonValue(QJsonValue::Null);

    qint64 n = raw.toInteger();
    if (isBool)
        return n != 0 ? 1 : 0;
    if (isSigned && n > 32767)
        n -= 65536;
    return roundTo(static_cast<double>(n) * scale, scaleDigits(scale));
}

bool inheritLimit(double registerLimit, double registerScale, double tagScale, double &out)
{
    if (!std::isfinite(registerLimit) || std::abs(registerLimit) >= 1e15)
        return false;   // "no limit" is sent as ±DBL_MAX
    const double regScale = registerScale != 0.0 ? registerScale : 1.0;
    out = roundTo(registerLimit / regScale * tagScale, scaleDigits(tagScale));
    return true;
}

//-----------------------------------------------------------//
// Values and quality
//-----------------------------------------------------------//
QString qualityToString(Quality q)
{
    switch (q) {
    case Quality::Good:   return QStringLiteral("good");
    case Quality::Normal: return QStringLiteral("normal");
    case Quality::Bad:    break;
    }
    return QStringLiteral("bad");
}

bool qualityFromString(const QString &s, Quality &out)
{
    if (s == QLatin1String("good"))     { out = Quality::Good;   return true; }
    if (s == QLatin1String("normal"))   { out = Quality::Normal; return true; }
    if (s == QLatin1String("bad") ||
        s == QLatin1String("disabled")) { out = Quality::Bad;    return true; }
    return false;
}

bool valueTypeFromString(const QString &s, ValueType &out)
{
    if (s == QLatin1String("bool"))   { out = ValueType::Bool;   return true; }
    if (s == QLatin1String("number")) { out = ValueType::Number; return true; }
    if (s == QLatin1String("string")) { out = ValueType::String; return true; }
    return false;
}

QString valueTypeToString(ValueType t)
{
    switch (t) {
    case ValueType::Bool:   return QStringLiteral("bool");
    case ValueType::Number: return QStringLiteral("number");
    case ValueType::String: break;
    }
    return QStringLiteral("string");
}

double toNumber(const QJsonValue &v)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    if (v.isBool())
        return v.toBool() ? 1.0 : 0.0;
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        const QString s = v.toString().trimmed();
        if (s.isEmpty())
            return kNaN;
        bool ok = false;
        const double d = s.toDouble(&ok);
        return (ok && std::isfinite(d)) ? d : kNaN;
    }
    return kNaN;
}

bool isTruthy(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return false;
    if (v.isBool())
        return v.toBool();
    if (v.isDouble()) {
        const double d = v.toDouble();
        return d != 0.0 && !std::isnan(d);
    }
    if (v.isString()) {
        const QString s = v.toString();
        return !(s.isEmpty() || s == QLatin1String("0") || s == QLatin1String("false"));
    }
    return true;   // arrays and objects
}

// JavaScript Number.prototype.toString(): fixed notation for 1e-6 <= |d| < 1e21
static QString jsNumber(double d)
{
    if (std::isnan(d))
        return QStringLiteral("NaN");
    if (std::isinf(d))
        return d > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (d == 0.0)
        return QStringLiteral("0");

    const double a = std::abs(d);
    if (a >= 1e-6 && a < 1e21)
        return QString::number(d, 'f', QLocale::FloatingPointShortest);

    QString s = QString::number(d, 'e', QLocale::FloatingPointShortest);   // 1.5e+21, 1e-07
    static const QRegularExpression kExp(QStringLiteral("e([+-])0*(\\d)"));
    s.replace(kExp, QStringLiteral("e\\1\\2"));
    return s;
}

QString toText(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined()) return QStringLiteral("null");
    if (v.isBool())                    return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())                  return jsNumber(v.toDouble());
    if (v.isString())                  return v.toString();
    return QString();
}

//-----------------------------------------------------------//
// Operators
//-----------------------------------------------------------//
namespace {

struct OpSpec {
    const char *name;
    int  minArgs;
    int  maxArgs;    // -1 = no upper limit
    bool odd;        // argument count must be odd (select)
    int  bitArg;     // index of the constant bit list, -1 = none
};

constexpr OpSpec kOps[] = {
    { "eq",      2,  2, false, -1 },
    { "ne",      2,  2, false, -1 },
    { "gt",      2,  2, false, -1 },
    { "ge",      2,  2, false, -1 },
    { "lt",      2,  2, false, -1 },
    { "le",      2,  2, false, -1 },
    { "range",   3,  3, false, -1 },
    { "bitAny",  2,  2, false,  1 },
    { "bitNone", 2,  2, false,  1 },
    { "and",     2, -1, false, -1 },
    { "or",      2, -1, false, -1 },
    { "not",     1,  1, false, -1 },
    { "select",  3, -1, true,  -1 },
    { "add",     2, -1, false, -1 },
    { "sub",     2,  2, false, -1 },
    { "mul",     2, -1, false, -1 },
    { "div",     2,  2, false, -1 },
    { "min",     2, -1, false, -1 },
    { "max",     2, -1, false, -1 },
    { "avg",     2, -1, false, -1 },
    { "abs",     1,  1, false, -1 },
};

const OpSpec *findOp(const QString &name)
{
    for (const OpSpec &op : kOps)
        if (name == QLatin1String(op.name))
            return &op;
    return nullptr;
}

bool tagIdValid(const QString &id)
{
    static const QRegularExpression kId(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return kId.match(id).hasMatch();
}

QString childPath(const QString &parent, int index)
{
    const QString self = QStringLiteral("args[%1]").arg(index);
    return parent.isEmpty() ? self : parent + QLatin1Char('.') + self;
}

bool fail(QString &error, QString &path, const QString &at, const QString &message)
{
    error = message;
    path  = at;
    return false;
}

// One node; bitList is true when the node is the bit-list argument.
bool validateNode(const QJsonValue &node, const QString &at, bool bitList,
                  ExprInfo &info, QString &error, QString &path)
{
    if (++info.nodes > kExprMaxNodes)
        return fail(error, path, at,
                    QStringLiteral("expression has more than %1 nodes").arg(kExprMaxNodes));

    if (!node.isObject())
        return fail(error, path, at, QStringLiteral("each node must be an object"));
    const QJsonObject obj = node.toObject();

    const int kinds = int(obj.contains(QLatin1String("tag")))
                    + int(obj.contains(QLatin1String("value")))
                    + int(obj.contains(QLatin1String("op")));
    if (kinds != 1)
        return fail(error, path, at,
                    QStringLiteral("a node must have exactly one of 'tag', 'value' or 'op'"));

    if (bitList) {
        const QJsonValue v = obj.value(QLatin1String("value"));
        if (!v.isArray())
            return fail(error, path, at,
                        QStringLiteral("the bit list must be a constant array, e.g. { \"value\": [1, 2] }"));
        for (const QJsonValue &b : v.toArray()) {
            const double d = b.toDouble(-1.0);
            if (!b.isDouble() || d != std::floor(d) || d < 0 || d > kBitMax)
                return fail(error, path, at,
                            QStringLiteral("bit numbers must be integers 0–%1").arg(kBitMax));
        }
        return true;
    }

    if (obj.contains(QLatin1String("tag"))) {
        const QJsonValue t = obj.value(QLatin1String("tag"));
        if (!t.isString() || !tagIdValid(t.toString()))
            return fail(error, path, at,
                        QStringLiteral("'tag' must be a tag id (1–64 characters: letters, digits, _ or -)"));
        info.refs.insert(t.toString());
        if (info.refs.size() > kExprMaxRefs)
            return fail(error, path, at,
                        QStringLiteral("expression refers to more than %1 tags").arg(kExprMaxRefs));
        return true;
    }

    if (obj.contains(QLatin1String("value"))) {
        const QJsonValue v = obj.value(QLatin1String("value"));
        if (v.isArray() || v.isObject())
            return fail(error, path, at,
                        QStringLiteral("'value' must be a number, string, true/false or null "
                                       "(arrays only as the bit list of bitAny/bitNone)"));
        return true;
    }

    const QJsonValue opv = obj.value(QLatin1String("op"));
    const OpSpec *op = opv.isString() ? findOp(opv.toString()) : nullptr;
    if (!op)
        return fail(error, path, at, QStringLiteral("unknown operator '%1'").arg(opv.toString()));

    const QJsonValue argsv = obj.value(QLatin1String("args"));
    if (!argsv.isArray())
        return fail(error, path, at, QStringLiteral("'%1': 'args' must be an array").arg(op->name));
    const QJsonArray args = argsv.toArray();
    const int n = args.size();

    const bool countOk = n >= op->minArgs
                      && (op->maxArgs < 0 || n <= op->maxArgs)
                      && (!op->odd || n % 2 == 1);
    if (!countOk) {
        QString want;
        if (op->odd)
            want = QStringLiteral("an odd number of arguments, at least %1").arg(op->minArgs);
        else if (op->maxArgs < 0)
            want = QStringLiteral("at least %1 arguments").arg(op->minArgs);
        else
            want = QStringLiteral("%1 argument(s)").arg(op->minArgs);
        return fail(error, path, at,
                    QStringLiteral("'%1' takes %2, got %3").arg(QLatin1String(op->name), want).arg(n));
    }

    for (int i = 0; i < n; ++i) {
        if (!validateNode(args.at(i), childPath(at, i), i == op->bitArg, info, error, path))
            return false;
    }
    return true;
}

} // namespace

bool validateExpr(const QJsonValue &expr, ExprInfo &info, QString &error, QString &path)
{
    info = ExprInfo();
    // "" (not a null string) so callers can always report the path, even for the root
    return validateNode(expr, QStringLiteral(""), false, info, error, path);
}

bool checkGraph(const QHash<QString, QSet<QString>> &exprRefs, QString &error)
{
    enum State { Unvisited, Visiting, Done };
    QHash<QString, State> state;
    QHash<QString, int>   depth;
    QStringList           stack;

    std::function<int(const QString &)> visit = [&](const QString &id) -> int {
        const State s = state.value(id, Unvisited);
        if (s == Done)
            return depth.value(id);
        if (s == Visiting) {
            QStringList cycle = stack.mid(stack.indexOf(id));
            cycle.append(id);
            error = QStringLiteral("circular reference: %1").arg(cycle.join(QStringLiteral(" → ")));
            return -1;
        }

        state[id] = Visiting;
        stack.append(id);
        int d = 1;
        QStringList refs = exprRefs.value(id).values();
        refs.sort();
        for (const QString &ref : refs) {
            if (!exprRefs.contains(ref))
                continue;   // register tag or missing: ends the chain
            const int r = visit(ref);
            if (r < 0)
                return -1;
            d = std::max(d, r + 1);
        }
        stack.removeLast();
        state[id] = Done;
        depth[id] = d;

        if (d > kExprMaxDepth) {
            error = QStringLiteral("computed tag '%1' is %2 levels deep (expr tags referring to "
                                   "expr tags); the limit is %3").arg(id).arg(d).arg(kExprMaxDepth);
            return -1;
        }
        return d;
    };

    QStringList ids = exprRefs.keys();
    ids.sort();
    for (const QString &id : ids)
        if (visit(id) < 0)
            return false;
    return true;
}

//-----------------------------------------------------------//
// Evaluation
//-----------------------------------------------------------//
namespace {

const Sample kBad{ QJsonValue(QJsonValue::Null), Quality::Bad };

bool same(const QJsonValue &a, const QJsonValue &b)
{
    const double na = toNumber(a), nb = toNumber(b);
    if (std::isfinite(na) && std::isfinite(nb))
        return na == nb;
    return toText(a) == toText(b);
}

bool compare(const QString &op, const QJsonValue &a, const QJsonValue &b)
{
    const double na = toNumber(a), nb = toNumber(b);
    if (!std::isfinite(na) || !std::isfinite(nb))
        return false;
    if (op == QLatin1String("gt")) return na >  nb;
    if (op == QLatin1String("ge")) return na >= nb;
    if (op == QLatin1String("lt")) return na <  nb;
    return na <= nb;   // le
}

// bitAny / bitNone: the value truncated to an integer, read as 32 bits like
// JavaScript's >> operator.
bool bitTest(bool any, const QJsonValue &v, const QJsonValue &bitList)
{
    const double a = toNumber(v);
    if (!std::isfinite(a))
        return false;

    QList<int> bits;
    for (const QJsonValue &b : bitList.toArray()) {
        const double d = b.toDouble(-1.0);
        if (b.isDouble() && d == std::floor(d) && d >= 0 && d <= 31)
            bits.append(static_cast<int>(d));
    }
    if (bits.isEmpty())
        return false;

    const double t = std::fmod(std::trunc(a), 4294967296.0);
    const quint32 w = static_cast<quint32>(static_cast<qint64>(t));
    bool hit = false;
    for (int b : bits)
        hit = hit || ((w >> b) & 1u);
    return any ? hit : !hit;
}

// add / sub / mul / div / min / max / avg / abs. Every argument must read as
// a number and the result must be finite (division by 0 is not); otherwise
// the result cannot be decided. Returns false for an operator that is not
// arithmetic.
bool arith(const QString &op, const QList<QJsonValue> &v, bool &ok, double &out)
{
    enum { Add, Sub, Mul, Div, Min, Max, Avg, Abs } kind;
    if      (op == QLatin1String("add")) kind = Add;
    else if (op == QLatin1String("sub")) kind = Sub;
    else if (op == QLatin1String("mul")) kind = Mul;
    else if (op == QLatin1String("div")) kind = Div;
    else if (op == QLatin1String("min")) kind = Min;
    else if (op == QLatin1String("max")) kind = Max;
    else if (op == QLatin1String("avg")) kind = Avg;
    else if (op == QLatin1String("abs")) kind = Abs;
    else return false;

    ok = false;
    const int n = v.size();
    if (n < 1 || (kind == Abs && n != 1) || (kind != Abs && n < 2)
        || ((kind == Sub || kind == Div) && n != 2))
        return true;

    QList<double> x;
    x.reserve(n);
    for (const QJsonValue &a : v) {
        const double d = toNumber(a);
        if (!std::isfinite(d))
            return true;
        x.append(d);
    }

    double r = x[0];
    switch (kind) {
    case Add: case Avg: for (int i = 1; i < n; ++i) r += x[i]; if (kind == Avg) r /= n; break;
    case Mul: for (int i = 1; i < n; ++i) r *= x[i]; break;
    case Min: for (int i = 1; i < n; ++i) r = std::min(r, x[i]); break;
    case Max: for (int i = 1; i < n; ++i) r = std::max(r, x[i]); break;
    case Sub: r = x[0] - x[1]; break;
    case Div: if (x[1] == 0.0) return true; r = x[0] / x[1]; break;
    case Abs: r = std::abs(x[0]); break;
    }
    if (!std::isfinite(r))
        return true;
    out = r;
    ok  = true;
    return true;
}

} // namespace

Evaluator::Evaluator(TagLookup lookup)
    : m_lookup(std::move(lookup))
{
}

Sample Evaluator::evalTag(const QString &id)
{
    auto it = m_used.constFind(id);
    if (it != m_used.constEnd())
        return it->sample;
    if (m_inProgress.contains(id))
        return kBad;   // circular reference (rejected on save; guarded here anyway)

    const TagInput in = m_lookup(id);
    UsedInput used;
    used.kind = in.kind;

    switch (in.kind) {
    case TagInput::Kind::Missing:
        used.sample = kBad;
        break;

    case TagInput::Kind::Register: {
        used.raw = in.raw;
        const QJsonValue v = toDisplay(in.raw, in.isBool, in.isSigned, in.scale);
        if (in.rawQ == Quality::Bad || v.isNull())
            used.sample = kBad;
        else
            used.sample = Sample{ v, in.rawQ };
        break;
    }

    case TagInput::Kind::Param: {
        const Sample &f = in.fixedSample;
        used.sample = (f.q == Quality::Bad || f.value.isNull() || f.value.isUndefined()) ? kBad : f;
        break;
    }

    case TagInput::Kind::Expr:
        if (in.fixed) {
            used.sample = in.fixedSample.q == Quality::Bad ? kBad : in.fixedSample;
        } else {
            m_inProgress.insert(id);
            used.sample = finish(evalNode(in.expr), in.valueType, in.decimals);
            m_inProgress.remove(id);
        }
        break;
    }

    m_used.insert(id, used);
    return used.sample;
}

Sample Evaluator::evalExpr(const QJsonValue &expr, ValueType valueType, int decimals)
{
    return finish(evalNode(expr), valueType, decimals);
}

// Converts the final result to the tag's value type. decimals rounds a
// number result only (intermediate values are never rounded); -0 becomes 0.
Sample Evaluator::finish(const Sample &s, ValueType valueType, int decimals) const
{
    if (s.q == Quality::Bad)
        return kBad;

    switch (valueType) {
    case ValueType::Bool:
        return Sample{ isTruthy(s.value), s.q };
    case ValueType::Number: {
        double d = toNumber(s.value);
        if (!std::isfinite(d))
            return kBad;
        if (decimals >= 0)
            d = roundTo(d, decimals);
        if (d == 0.0)
            d = 0.0;   // drop the sign of -0
        return Sample{ d, s.q };
    }
    case ValueType::String:
        break;
    }
    return Sample{ toText(s.value), s.q };
}

Sample Evaluator::evalNode(const QJsonValue &node)
{
    if (!node.isObject())
        return kBad;
    const QJsonObject obj = node.toObject();

    if (obj.contains(QLatin1String("tag")))
        return evalTag(obj.value(QLatin1String("tag")).toString());
    if (obj.contains(QLatin1String("value")))
        return Sample{ obj.value(QLatin1String("value")), Quality::Good };

    const QString   op   = obj.value(QLatin1String("op")).toString();
    const QJsonArray args = obj.value(QLatin1String("args")).toArray();

    // Every argument is evaluated (no short-circuit): any bad input makes the
    // result bad, even when the other side would decide it.
    QList<QJsonValue> v;
    v.reserve(args.size());
    Quality q = Quality::Good;
    for (const QJsonValue &a : args) {
        const Sample s = evalNode(a);
        q = std::max(q, s.q);
        v.append(s.value);
    }
    if (q == Quality::Bad)
        return kBad;

    const int n = v.size();
    auto result = [q](const QJsonValue &value) { return Sample{ value, q }; };

    if (op == QLatin1String("eq") && n == 2) return result(same(v[0], v[1]));
    if (op == QLatin1String("ne") && n == 2) return result(!same(v[0], v[1]));
    if ((op == QLatin1String("gt") || op == QLatin1String("ge") ||
         op == QLatin1String("lt") || op == QLatin1String("le")) && n == 2)
        return result(compare(op, v[0], v[1]));

    if (op == QLatin1String("range") && n == 3) {
        const double a = toNumber(v[0]);
        if (!std::isfinite(a))
            return result(false);
        const bool lowOk  = v[1].isNull() || a >= toNumber(v[1]);
        const bool highOk = v[2].isNull() || a <= toNumber(v[2]);
        return result(lowOk && highOk);
    }

    if (op == QLatin1String("bitAny")  && n == 2) return result(bitTest(true,  v[0], v[1]));
    if (op == QLatin1String("bitNone") && n == 2) return result(bitTest(false, v[0], v[1]));

    if (op == QLatin1String("and") && n >= 2) {
        bool all = true;
        for (const QJsonValue &x : v) all = all && isTruthy(x);
        return result(all);
    }
    if (op == QLatin1String("or") && n >= 2) {
        bool any = false;
        for (const QJsonValue &x : v) any = any || isTruthy(x);
        return result(any);
    }
    if (op == QLatin1String("not") && n == 1)
        return result(!isTruthy(v[0]));

    if (op == QLatin1String("select") && n >= 3 && n % 2 == 1) {
        for (int i = 0; i + 1 < n; i += 2)
            if (isTruthy(v[i]))
                return result(v[i + 1]);
        return result(v[n - 1]);
    }

    bool   ok = false;
    double r  = 0.0;
    if (arith(op, v, ok, r))
        return ok ? result(r) : kBad;

    return kBad;   // unknown operator or wrong argument count
}

//-----------------------------------------------------------//
// Setting (param) tags
//-----------------------------------------------------------//
static double noNegZero(double d)
{
    return d == 0.0 ? 0.0 : d;
}

ParamStatus parseParamDef(const QJsonValue &valueType, const QJsonValue &initial,
                          const QJsonValue &min, const QJsonValue &max,
                          const QJsonValue &decimals, ParamDef &out, QString &error)
{
    out = ParamDef();
    if (!valueTypeFromString(valueType.toString(), out.vt)) {
        error = QStringLiteral("'valueType' must be \"bool\", \"number\" or \"string\"");
        return ParamStatus::BadField;
    }
    if (initial.isUndefined()) {
        error = QStringLiteral("'initial' is required");
        return ParamStatus::BadField;
    }

    if (out.vt == ValueType::Number) {
        auto limit = [&error](const QJsonValue &v, const char *name, bool &has, double &d) {
            if (v.isUndefined() || v.isNull())
                return true;
            if (!v.isDouble()) {
                error = QStringLiteral("'%1' must be a number or null").arg(QLatin1String(name));
                return false;
            }
            has = true;
            d   = v.toDouble();
            return true;
        };
        if (!limit(min, "min", out.hasMin, out.min) || !limit(max, "max", out.hasMax, out.max))
            return ParamStatus::BadField;
        if (!decimalsFromJson(decimals, out.decimals)) {
            error = QStringLiteral("'decimals' must be an integer 0–%1 or null").arg(kDecimalsMax);
            return ParamStatus::BadRule;
        }
        if (out.hasMin && out.hasMax && out.min > out.max) {
            error = QStringLiteral("'min' (%1) is greater than 'max' (%2)")
                        .arg(toText(out.min), toText(out.max));
            return ParamStatus::BadRule;
        }
    }

    bool outOfRange = false;
    if (!acceptParamValue(out, initial, QStringLiteral("initial"), out.initial, error, outOfRange))
        return ParamStatus::BadRule;
    return ParamStatus::Ok;
}

bool acceptParamValue(const ParamDef &def, const QJsonValue &v, const QString &what,
                      QJsonValue &out, QString &error, bool &outOfRange)
{
    outOfRange = false;
    switch (def.vt) {
    case ValueType::Bool:
        if (!v.isBool()) {
            error = QStringLiteral("'%1' must be true or false").arg(what);
            return false;
        }
        out = v;
        return true;

    case ValueType::String:
        if (!v.isString()) {
            error = QStringLiteral("'%1' must be a string").arg(what);
            return false;
        }
        if (v.toString().size() > kParamTextMax) {
            error = QStringLiteral("'%1' must be at most %2 characters").arg(what).arg(kParamTextMax);
            return false;
        }
        out = v;
        return true;

    case ValueType::Number:
        break;
    }

    if (!v.isDouble()) {
        error = QStringLiteral("'%1' must be a number").arg(what);
        return false;
    }
    double d = v.toDouble();
    if (def.decimals >= 0)
        d = roundTo(d, def.decimals);
    d   = noNegZero(d);
    out = d;
    if ((def.hasMin && d < def.min) || (def.hasMax && d > def.max)) {
        outOfRange = true;
        error = QStringLiteral("'%1' %2 is outside the allowed range").arg(what, toText(d));
        return false;
    }
    return true;
}

QJsonValue carryParamValue(const ParamDef &def, bool sameType, const QJsonValue &oldValue,
                           ParamCarry &how)
{
    if (!sameType) {
        how = ParamCarry::TypeChanged;
        return def.initial;
    }
    QJsonValue out;
    QString    error;
    bool       outOfRange = false;
    if (acceptParamValue(def, oldValue, QStringLiteral("value"), out, error, outOfRange)) {
        how = (out == oldValue) ? ParamCarry::Kept : ParamCarry::Rounded;
        return out;
    }
    if (!outOfRange) {
        how = ParamCarry::Reset;   // unreadable stored value
        return def.initial;
    }
    const double d = out.toDouble();
    how = ParamCarry::Clamped;
    return (def.hasMin && d < def.min) ? def.min : def.max;
}

QString paramToText(const QJsonValue &v)
{
    const QByteArray a = QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));   // strip [ ]
}

QJsonValue paramFromText(const QString &text)
{
    if (text.isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray("[") + text.toUtf8() + "]");
    if (!doc.isArray() || doc.array().size() != 1)
        return QJsonValue(QJsonValue::Undefined);
    return doc.array().at(0);
}

QString paramLogText(const QJsonValue &v)
{
    if (v.isString())
        return QLatin1Char('"') + v.toString() + QLatin1Char('"');
    if (v.isUndefined())
        return QStringLiteral("(none)");
    return toText(v);
}

} // namespace Hmi
