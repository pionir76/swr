//-----------------------------------------------------------//
// Host-side test for hmi/HmiExpr: runs the shared HMI test data
// (swr_web/referencs/hmi-testdata-convert.json, hmi-testdata-expr.json)
// plus validation cases the shared data does not cover.
//
//   hmi_expr_test <convert.json> <expr.json>
//
// Exit code 0 when every case passes.
//-----------------------------------------------------------//
#include "../../hmi/HmiExpr.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cmath>

using namespace Hmi;

static int g_failed = 0;
static int g_passed = 0;

static QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

static void check(bool ok, const QString &name, const QString &detail = QString())
{
    if (ok) {
        ++g_passed;
    } else {
        ++g_failed;
        out() << "FAIL  " << name << (detail.isEmpty() ? QString() : QStringLiteral("  — ") + detail) << Qt::endl;
    }
}

static QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        out() << "cannot open " << path << Qt::endl;
        std::exit(2);
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

static bool sameValue(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isDouble() && b.isDouble())
        return std::abs(a.toDouble() - b.toDouble()) < 1e-12 && a.toDouble() == b.toDouble();
    return a == b;
}

static QString show(const QJsonValue &v)
{
    if (v.isString()) return QLatin1Char('"') + v.toString() + QLatin1Char('"');
    if (v.isNull())   return QStringLiteral("null");
    return toText(v);
}

static bool isBoolType(const QString &registerType)
{
    return registerType == QLatin1String("coil") || registerType == QLatin1String("discrete_input");
}

//-----------------------------------------------------------//
static void runConvert(const QJsonObject &doc)
{
    for (const QJsonValue &cv : doc.value(QLatin1String("cases")).toArray()) {
        const QJsonObject c = cv.toObject();
        const QJsonValue tagScale = c.value(QLatin1String("tagScale"));
        const double scale = tagScale.isDouble() ? tagScale.toDouble()
                                                 : c.value(QLatin1String("registerScale")).toDouble(1.0);
        const QJsonValue got = toDisplay(c.value(QLatin1String("raw")),
                                         isBoolType(c.value(QLatin1String("registerType")).toString()),
                                         c.value(QLatin1String("isSigned")).toBool(),
                                         scale);
        const QJsonValue want = c.value(QLatin1String("expect"));
        check(sameValue(got, want), QStringLiteral("convert: ") + c.value(QLatin1String("name")).toString(),
              QStringLiteral("got %1, want %2").arg(show(got), show(want)));
    }
}

//-----------------------------------------------------------//
static void runExpr(const QJsonObject &doc)
{
    const QJsonObject tags = doc.value(QLatin1String("tags")).toObject();

    // The expr tags in the shared data must pass validation and the graph check
    QHash<QString, QSet<QString>> graph;
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        const QJsonObject t = it.value().toObject();
        if (t.value(QLatin1String("kind")).toString() != QLatin1String("expr"))
            continue;
        ExprInfo info;
        QString err, path;
        const bool ok = validateExpr(t.value(QLatin1String("source")).toObject().value(QLatin1String("expr")),
                                     info, err, path);
        check(ok, QStringLiteral("validate tag ") + it.key(), err + QStringLiteral(" at ") + path);
        graph.insert(it.key(), info.refs);
    }
    QString graphErr;
    check(checkGraph(graph, graphErr), QStringLiteral("graph of shared tags"), graphErr);

    for (const QJsonValue &cv : doc.value(QLatin1String("cases")).toArray()) {
        const QJsonObject c      = cv.toObject();
        const QString     name   = c.value(QLatin1String("name")).toString();
        const QJsonObject inputs = c.value(QLatin1String("inputs")).toObject();

        TagLookup lookup = [&](const QString &id) {
            TagInput in;
            if (!tags.contains(id))
                return in;   // Missing
            const QJsonObject t   = tags.value(id).toObject();
            const QJsonObject src = t.value(QLatin1String("source")).toObject();
            if (t.value(QLatin1String("kind")).toString() == QLatin1String("expr")) {
                in.kind = TagInput::Kind::Expr;
                valueTypeFromString(src.value(QLatin1String("valueType")).toString(), in.valueType);
                in.expr = src.value(QLatin1String("expr"));
                return in;
            }
            in.kind     = TagInput::Kind::Register;
            in.isBool   = isBoolType(src.value(QLatin1String("registerType")).toString());
            in.isSigned = src.value(QLatin1String("isSigned")).toBool();
            const QJsonValue ts = t.value(QLatin1String("scale"));
            in.scale    = ts.isDouble() ? ts.toDouble() : src.value(QLatin1String("scale")).toDouble(1.0);
            const QJsonObject inp = inputs.value(id).toObject();
            in.raw  = inp.value(QLatin1String("raw"));
            if (in.raw.isUndefined()) in.raw = QJsonValue(QJsonValue::Null);
            if (!qualityFromString(inp.value(QLatin1String("q")).toString(), in.rawQ))
                in.rawQ = Quality::Bad;
            return in;
        };

        ExprInfo info;
        QString err, path;
        check(validateExpr(c.value(QLatin1String("expr")), info, err, path),
              QStringLiteral("validate case: ") + name, err + QStringLiteral(" at ") + path);

        ValueType vt = ValueType::Bool;
        valueTypeFromString(c.value(QLatin1String("valueType")).toString(), vt);

        Evaluator ev(lookup);
        const Sample got = ev.evalExpr(c.value(QLatin1String("expr")), vt);
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        const bool ok = sameValue(got.value, want.value(QLatin1String("value")))
                     && qualityToString(got.q) == want.value(QLatin1String("q")).toString();
        check(ok, QStringLiteral("expr: ") + name,
              QStringLiteral("got %1/%2, want %3/%4")
                  .arg(show(got.value), qualityToString(got.q),
                       show(want.value(QLatin1String("value"))), want.value(QLatin1String("q")).toString()));
    }
}

//-----------------------------------------------------------//
// Validation and helper cases not in the shared data
//-----------------------------------------------------------//
static QJsonValue parse(const char *json)
{
    return QJsonDocument::fromJson(QByteArray("{\"x\":") + json + "}").object().value(QLatin1String("x"));
}

static void expectInvalid(const char *json, const QString &wantPath, const QString &name)
{
    ExprInfo info;
    QString err, path;
    const bool ok = validateExpr(parse(json), info, err, path);
    check(!ok && path == wantPath, QStringLiteral("invalid: ") + name,
          ok ? QStringLiteral("accepted") : QStringLiteral("path '%1' (%2), want '%3'").arg(path, err, wantPath));
}

static void runLocal()
{
    expectInvalid(R"({"op":"nope","args":[]})", QString(), QStringLiteral("unknown operator"));
    expectInvalid(R"({"op":"eq","args":[{"tag":"A"}]})", QString(), QStringLiteral("eq with one argument"));
    expectInvalid(R"({"op":"and","args":[{"tag":"A"},{"op":"bitAny","args":[{"tag":"B"},{"value":[16]}]}]})",
                  QStringLiteral("args[1].args[1]"), QStringLiteral("bit 16"));
    expectInvalid(R"({"op":"bitAny","args":[{"tag":"B"},{"tag":"C"}]})",
                  QStringLiteral("args[1]"), QStringLiteral("bit list from a tag"));
    expectInvalid(R"({"op":"select","args":[{"value":true},{"value":1}]})", QString(),
                  QStringLiteral("select with an even count"));
    expectInvalid(R"({"op":"eq","args":[{"tag":"A"},{"value":[1]}]})",
                  QStringLiteral("args[1]"), QStringLiteral("array outside a bit list"));
    expectInvalid(R"({"tag":"A","value":1})", QString(), QStringLiteral("two node kinds"));
    expectInvalid(R"({"tag":"bad id"})", QString(), QStringLiteral("bad tag id"));
    expectInvalid(R"("A")", QString(), QStringLiteral("node not an object"));

    // 65 nodes: or of 64 constants
    {
        QJsonArray args;
        for (int i = 0; i < 64; ++i) args.append(QJsonObject{ { QStringLiteral("value"), i } });
        const QJsonObject big{ { QStringLiteral("op"), QStringLiteral("or") }, { QStringLiteral("args"), args } };
        ExprInfo info; QString err, path;
        check(!validateExpr(big, info, err, path), QStringLiteral("invalid: 65 nodes"));
    }
    // 33 distinct tags
    {
        QJsonArray args;
        for (int i = 0; i < 33; ++i) args.append(QJsonObject{ { QStringLiteral("tag"), QStringLiteral("T%1").arg(i) } });
        const QJsonObject big{ { QStringLiteral("op"), QStringLiteral("and") }, { QStringLiteral("args"), args } };
        ExprInfo info; QString err, path;
        check(!validateExpr(big, info, err, path), QStringLiteral("invalid: 33 tags"));
    }

    // Graph: cycle and depth
    {
        QHash<QString, QSet<QString>> g;
        g.insert(QStringLiteral("A"), { QStringLiteral("B") });
        g.insert(QStringLiteral("B"), { QStringLiteral("C"), QStringLiteral("REG") });
        g.insert(QStringLiteral("C"), { QStringLiteral("A") });
        QString err;
        check(!checkGraph(g, err) && err.contains(QStringLiteral("circular")), QStringLiteral("graph: cycle"), err);

        QHash<QString, QSet<QString>> self;
        self.insert(QStringLiteral("A"), { QStringLiteral("A") });
        check(!checkGraph(self, err), QStringLiteral("graph: self reference"), err);

        QHash<QString, QSet<QString>> chain;
        for (int i = 0; i < 8; ++i)
            chain.insert(QStringLiteral("E%1").arg(i), { QStringLiteral("E%1").arg(i + 1) });
        chain.insert(QStringLiteral("E8"), { QStringLiteral("REG") });   // E0 is 9 deep
        check(!checkGraph(chain, err) && err.contains(QStringLiteral("deep")), QStringLiteral("graph: 9 deep"), err);
        chain.remove(QStringLiteral("E8"));                                 // E0 is 8 deep
        check(checkGraph(chain, err), QStringLiteral("graph: 8 deep is fine"), err);
    }

    // Text form matches JavaScript String()
    check(toText(123.4) == QLatin1String("123.4"), QStringLiteral("text 123.4"), toText(123.4));
    check(toText(1e21) == QLatin1String("1e+21"), QStringLiteral("text 1e21"), toText(1e21));
    check(toText(15000000000.0) == QLatin1String("15000000000"), QStringLiteral("text 1.5e10"), toText(15000000000.0));
    check(toText(-5.0) == QLatin1String("-5"), QStringLiteral("text -5"), toText(-5.0));
    check(toText(1e-7) == QLatin1String("1e-7"), QStringLiteral("text 1e-7"), toText(1e-7));
    check(toText(0.000001) == QLatin1String("0.000001"), QStringLiteral("text 1e-6"), toText(0.000001));

    // Inherited write limits
    double lim = 0;
    check(inheritLimit(100.0, 0.1, 0.01, lim) && lim == 10.0, QStringLiteral("inherit 100 @0.1 -> @0.01"));
    check(!inheritLimit(1.7976931348623157e308, 1, 1, lim), QStringLiteral("inherit: no limit"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        out() << "usage: hmi_expr_test <convert.json> <expr.json>" << Qt::endl;
        return 2;
    }
    runConvert(readJson(QString::fromLocal8Bit(argv[1])));
    runExpr(readJson(QString::fromLocal8Bit(argv[2])));
    runLocal();

    out() << g_passed << " passed, " << g_failed << " failed" << Qt::endl;
    return g_failed == 0 ? 0 : 1;
}
