//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/arch/stackTrace.h"
#include "pxr/base/arch/vsnprintf.h"
#include "pxr/base/tf/mallocTag.h"
#include "pxr/base/tf/ostreamMethods.h"
#include "pxr/base/tf/patternMatcher.h"
#include "pxr/base/tf/pxrCLI11/CLI11.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/scopeDescription.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/layer.h"

#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

using namespace pxr_CLI;

using std::string;
using std::vector;
using std::ostream;
using std::pair;
using std::unordered_map;
using std::unordered_set;

namespace {

string progName;

void VErr(char const *fmt, va_list ap) {
    fprintf(stderr, "%s: Error - %s\n", progName.c_str(),
            ArchVStringPrintf(fmt, ap).c_str());
}

void VPrint(char const *fmt, va_list ap) { vprintf(fmt, ap); }

void Err(char const *fmt, ...) ARCH_PRINTF_FUNCTION(1, 2);
void Err(char const *fmt, ...) {
    va_list ap; va_start(ap, fmt); VErr(fmt, ap); va_end(ap);
}

void ErrExit(char const *fmt, ...) ARCH_PRINTF_FUNCTION(1, 2);
void ErrExit(char const *fmt, ...) {
    va_list ap; va_start(ap, fmt); VErr(fmt, ap); va_end(ap);
    exit(1);
}

void Print(char const *fmt, ...) ARCH_PRINTF_FUNCTION(1, 2);
void Print(char const *fmt, ...) {
    va_list ap; va_start(ap, fmt); VPrint(fmt, ap); va_end(ap);
}

bool IsClose(double a, double b, double tol)
{
    double absDiff = fabs(a-b);
    return absDiff <= fabs(tol*a) || absDiff < fabs(tol*b);
}

// Convert string to double like std::stod, but throws an exception if
// the string has trailing characters that don't contribute to the double
// representation, like "42.0a".
double StringToDouble(const std::string &s)
{
    size_t pos;
    const double d = std::stod(s, &pos);
    if (pos != s.size()) {
        throw std::invalid_argument(
            TfStringPrintf("invalid string '%s'", s.c_str()));
    }
    return d;
}

struct SortKey {
    SortKey() : key("path") {}
    SortKey(string key) : key(key) {}
    string key;
};

// Parse times and time ranges in timeSpecs, throw an exception if something
// goes wrong.
void
ParseTimes(vector<string> const &timeSpecs,
           vector<double> *literalTimes,
           vector<pair<double, double>> *timeRanges)
{
    for (auto const &spec: timeSpecs) {
        try {
            if (TfStringContains(spec, "..")) {
                auto elts = TfStringSplit(spec, "..");
                if (elts.size() != 2) {
                    throw std::invalid_argument(
                        TfStringPrintf("invalid time syntax '%s'",
                                       spec.c_str()));
                }
                timeRanges->emplace_back(StringToDouble(elts[0]),
                                         StringToDouble(elts[1]));
            } else {
                literalTimes->emplace_back(StringToDouble(spec));
            }
        } catch (std::invalid_argument const &) {
            throw std::invalid_argument(
                TfStringPrintf("invalid time syntax '%s'", spec.c_str()));
        } catch (std::out_of_range const &) {
            throw std::invalid_argument(
                TfStringPrintf("time out of range '%s'", spec.c_str()));
        }
    }
    sort(literalTimes->begin(), literalTimes->end());
    literalTimes->erase(unique(literalTimes->begin(), literalTimes->end()),
                        literalTimes->end());
    sort(timeRanges->begin(), timeRanges->end());
    timeRanges->erase(unique(timeRanges->begin(), timeRanges->end()),
                      timeRanges->end());
}

struct ReportParams
{
    TfPatternMatcher *pathMatcher = nullptr;
    TfPatternMatcher *fieldMatcher = nullptr;
    SortKey sortKey;
    vector<double> literalTimes;
    vector<pair<double, double>> timeRanges;
    double timeTolerance;
    bool showSummary = false;
    bool validate = false;
    bool showValues = true;
    bool fullArrays = false;
};

struct SummaryStats
{
    size_t numSpecs = 0;
    size_t numPrimSpecs = 0;
    size_t numPropertySpecs = 0;
    size_t numFields = 0;
    size_t numSampleTimes = 0;
};

SummaryStats
GetSummaryStats(SdfLayerHandle const &layer)
{
    SummaryStats stats;
    layer->Traverse(
        SdfPath::AbsoluteRootPath(), [&stats, &layer](SdfPath const &path) {
            ++stats.numSpecs;
            stats.numPrimSpecs += path.IsPrimPath();
            stats.numPropertySpecs += path.IsPropertyPath();
            stats.numFields += layer->ListFields(path).size();
        });
    stats.numSampleTimes = layer->ListAllTimeSamples().size();
    return stats;
}

vector<SdfPath>
CollectPaths(SdfLayerHandle const &layer, ReportParams const &p)
{
    vector<SdfPath> result;
    layer->Traverse(SdfPath::AbsoluteRootPath(),
                    [&result, &p](SdfPath const &path) {
                        if (p.pathMatcher->Match(path.GetString()))
                            result.push_back(path);
                    });
    return result;
}

vector<TfToken>
CollectFields(SdfLayerHandle const &layer, SdfPath const &path,
              ReportParams const &p)
{
    vector<TfToken> fields = layer->ListFields(path);
    fields.erase(remove_if(fields.begin(), fields.end(),
                           [&p](TfToken const &f) {
                               return !p.fieldMatcher->Match(f.GetString());
                           }),
                 fields.end());
    return fields;
}

string
GetValueString(VtValue const &value, ReportParams const &p)
{
    return (!p.fullArrays && value.IsArrayValued()) ?
        TfStringPrintf("%s size %zu", value.GetTypeName().c_str(),
                       value.GetArraySize()) :
        TfStringPrintf("%s = %s", value.GetTypeName().c_str(),
                       TfStringify(value).c_str());
}

string
GetTimeSamplesValueString(SdfLayerHandle const &layer,
                          SdfPath const &path, ReportParams const &p)
{
    // Pull all the sample times for the given path, then collect up by p's
    // literalTimes and timeRanges, then if we're pulling values we'll do that
    // for each sample, and format.
    bool takeAllTimes = p.literalTimes.empty() && p.timeRanges.empty();
    auto times = layer->ListTimeSamplesForPath(path);
    vector<double> selectedTimes;
    selectedTimes.reserve(times.size());

    if (takeAllTimes) {
        selectedTimes.assign(times.begin(), times.end());
    }
    else {
        for (auto time: times) {
            // Check literalTimes.
            auto rng = equal_range(
                p.literalTimes.begin(), p.literalTimes.end(), time,
                [&p](double a, double b)  {
                    return IsClose(a, b, p.timeTolerance) ? false : a < b;
                });
            if (rng.first != rng.second)
                selectedTimes.push_back(time);

            // Check ranges.
            for (auto const &range: p.timeRanges) {
                if (range.first <= time && time <= range.second)
                    selectedTimes.push_back(time);
            }
        }
    }

    if (selectedTimes.empty())
        return "<no samples at requested times>";

    vector<string> result;
    VtValue val;
    for (auto time: selectedTimes) {
        TF_VERIFY(layer->QueryTimeSample(path, time, &val));
        result.push_back(
            TfStringPrintf("%s: %s", TfStringify(time).c_str(),
                           GetValueString(val, p).c_str()));
    }
    return TfStringPrintf("[\n        %s ]",
                          TfStringJoin(result, "\n        ").c_str());
}
                          
string
GetFieldValueString(SdfLayerHandle const &layer,
                    SdfPath const &path, TfToken const &field,
                    ReportParams const &p)
{
    string result;
    // Handle timeSamples specially:
    if (field == SdfFieldKeys->TimeSamples) {
        result = GetTimeSamplesValueString(layer, path, p);
    } else {
        VtValue value;
        TF_VERIFY(layer->HasField(path, field, &value));
        result = GetValueString(value, p);
    }
    return result;
}

void
GetReportByPath(SdfLayerHandle const &layer, ReportParams const &p,
                vector<string> &report)
{
    vector<SdfPath> paths = CollectPaths(layer, p);
    sort(paths.begin(), paths.end());
    for (auto const &path: paths) {
        SdfSpecType specType = layer->GetSpecType(path);
        report.push_back(
            TfStringPrintf(
                "<%s> : %s", 
                path.GetText(), TfStringify(specType).c_str()));

        vector<TfToken> fields = CollectFields(layer, path, p);
        if (fields.empty())
            continue;
        for (auto const &field: fields) {
            if (p.showValues) {
                report.push_back(
                    TfStringPrintf(
                        "  %s: %s", field.GetText(),
                        GetFieldValueString(layer, path, field, p).c_str())
                    );
            } else {
                report.push_back(TfStringPrintf("  %s", field.GetText()));
            }
        }
    }
}

void
GetReportByField(SdfLayerHandle const &layer, ReportParams const &p,
                 vector<string> &report)
{
    vector<SdfPath> paths = CollectPaths(layer, p);
    unordered_map<string, vector<string>> pathsByFieldString;
    unordered_set<string> allFieldStrings;
    sort(paths.begin(), paths.end());
    for (auto const &path: paths) {
        vector<TfToken> fields = CollectFields(layer, path, p);
        if (fields.empty())
            continue;
        for (auto const &field: fields) {
            string fieldString;
            if (p.showValues) {
                fieldString = TfStringPrintf(
                    "%s: %s", field.GetText(),
                    GetFieldValueString(layer, path, field, p).c_str());
            } else {
                fieldString = TfStringPrintf("%s", field.GetText());
            }
            pathsByFieldString[fieldString].push_back(
                TfStringPrintf("  <%s>", path.GetText()));
            allFieldStrings.insert(fieldString);
        }
    }
    vector<string> fsvec(allFieldStrings.begin(), allFieldStrings.end());
    sort(fsvec.begin(), fsvec.end());

    for (auto const &fs: fsvec) {
        report.push_back(fs);
        auto const &ps = pathsByFieldString[fs];
        report.insert(report.end(), ps.begin(), ps.end());
    }
}


void
Validate(SdfLayerHandle const &layer, ReportParams const &p,
         vector<string> &report)
{
    TfErrorMark m;
    TF_DESCRIBE_SCOPE("Collecting paths in @%s@",
                      layer->GetIdentifier().c_str());
    vector<SdfPath> paths;
    layer->Traverse(SdfPath::AbsoluteRootPath(),
                    [&paths, layer](SdfPath const &path) {
                        TF_DESCRIBE_SCOPE(
                            "Collecting path <%s> in @%s@",
                            path.GetText(), layer->GetIdentifier().c_str());
                        paths.push_back(path);
                    });
    sort(paths.begin(), paths.end());
    for (auto const &path: paths) {
        TF_DESCRIBE_SCOPE("Collecting fields for <%s> in @%s@",
                     path.GetText(), layer->GetIdentifier().c_str());
        vector<TfToken> fields = layer->ListFields(path);
        if (fields.empty())
            continue;
        for (auto const &field: fields) {
            VtValue value;
            if (field == SdfFieldKeys->TimeSamples) {
                // Pull each sample value individually.
                TF_DESCRIBE_SCOPE(
                    "Getting sample times for '%s' on <%s> in @%s@",
                    field.GetText(), path.GetText(),
                    layer->GetIdentifier().c_str());
                auto times = layer->ListTimeSamplesForPath(path);

                for (auto time: times) {
                    TF_DESCRIBE_SCOPE("Getting sample value at time "
                                      "%f for '%s' on <%s> in @%s@",
                                      time, field.GetText(), path.GetText(),
                                      layer->GetIdentifier().c_str());
                    layer->QueryTimeSample(path, time, &value);
                }
            } else {
                // Just pull value.
                TF_DESCRIBE_SCOPE("Getting value for '%s' on <%s> in @%s@",
                                  field.GetText(), path.GetText(),
                                  layer->GetIdentifier().c_str());
                layer->HasField(path, field, &value);
            }
        }
    }
    report.back() += m.IsClean() ? " - OK" : " - ERROR";
}

void Report(SdfLayerHandle layer, ReportParams const &p)
{
    vector<string> report = {
        TfStringPrintf("@%s@", layer->GetIdentifier().c_str()) };
    if (p.showSummary) {
        auto stats = GetSummaryStats(layer);
        report.push_back(
            TfStringPrintf(
                "  %zu specs, %zu prim specs, %zu property specs, %zu fields, "
                "%zu sample times",
                stats.numSpecs, stats.numPrimSpecs, stats.numPropertySpecs,
                stats.numFields, stats.numSampleTimes));
    } else if (p.validate) {
        Validate(layer, p, report);
    } else if (p.sortKey.key == "path") {
        GetReportByPath(layer, p, report);
    } else if (p.sortKey.key == "field") {
        GetReportByField(layer, p, report);
    }

    for (auto const &str: report) {
        Print("%s\n", str.c_str());
    }
}

} // anon

PXR_NAMESPACE_CLOSE_SCOPE

int
main(int argc, char const *argv[])
{
    PXR_NAMESPACE_USING_DIRECTIVE

    progName = TfGetBaseName(argv[0]);
    CLI::App app("Filter and display raw layer data", progName);

    bool showSummary = false, validate = false,
        fullArrays = false, noValues = false;
    SortKey sortKey("path");
    string pathRegex = ".*", fieldRegex = ".*";
    vector<string> timeSpecs, inputFiles;
    vector<double> literalTimes;
    vector<pair<double, double>> timeRanges;
    double timeTolerance = 1.25e-4; // ugh -- chosen to print well in help.

    app.add_option(
        "inputFiles", inputFiles, "The input files to dump.")
        ->required(true)
        ->option_text("...");
    app.add_flag(
        "-s,--summary", showSummary, "Report a high-level summary.");
    app.add_flag(
        "--validate", validate, 
        "Check validity by trying to read all data values.");
    app.add_option(
        "-p,--path", pathRegex, "Report only paths matching this regex.")
        ->option_text("regex");
    app.add_option(
        "-f,--field", fieldRegex, "Report only fields matching this regex.")
        ->option_text("regex");
    app.add_option(
        "-t,--time", timeSpecs,
        "Report only these times (n) or time ranges (ff..lf) for "
        "'timeSamples' fields")
        ->option_text("n or ff..lf");
    app.add_option(
        "--timeTolerance", timeTolerance, 
        "Report times that are close to those requested within this "
        "relative tolerance. Default: " + std::to_string(timeTolerance))
        ->default_val(timeTolerance)
        ->option_text("tol");
    app.add_option(
        "--sortBy", sortKey.key,
        "Group output by either path or field. Default: " + sortKey.key)
        ->default_val(sortKey.key)
        ->check(CLI::IsMember({"field", "path"}))
        ->option_text("path|field");
    app.add_flag(
        "--noValues", noValues, "Do not report field values.");
    app.add_flag(
        "--fullArrays", fullArrays,
        "Report full array contents rather than number of elements");

    CLI11_PARSE(app, argc, argv);

    try {
        ParseTimes(timeSpecs, &literalTimes, &timeRanges);
    } catch (std::exception const &e) {
        ErrExit("%s", e.what());
    }

    TfPatternMatcher pathMatcher(pathRegex);
    if (!pathMatcher.IsValid()) {
        ErrExit("path regex '%s' : %s", pathRegex.c_str(),
                pathMatcher.GetInvalidReason().c_str());
    }

    TfPatternMatcher fieldMatcher(fieldRegex);
    if (!fieldMatcher.IsValid()) {
        ErrExit("field regex '%s' : %s", fieldRegex.c_str(),
                fieldMatcher.GetInvalidReason().c_str());
    }
                
    ReportParams params;
    params.showSummary = showSummary;
    params.validate = validate;
    params.pathMatcher = &pathMatcher;
    params.fieldMatcher = &fieldMatcher;
    params.sortKey = sortKey;
    params.literalTimes = literalTimes;
    params.timeRanges = timeRanges;
    params.showValues = !noValues;
    params.fullArrays = fullArrays;
    params.timeTolerance = timeTolerance;

    // If malloc tags is enabled, keep layers alive so we can report per-layer
    // allocations properly.
    std::vector<SdfLayerRefPtr> keepAlive;
    bool mallocTagsEnabled = TfMallocTag::IsInitialized();
    
    for (auto const &file: inputFiles) {
        TF_DESCRIBE_SCOPE("Opening layer @%s@", file.c_str());
        TfAutoMallocTag tag(TfStringPrintf("Opening layer @%s@", file.c_str()));
        auto layer = SdfLayer::FindOrOpen(file);
        if (!layer) {
            Err("failed to open layer <%s>", file.c_str());
            continue;
        }
        Report(layer, params);
        if (mallocTagsEnabled) {
            keepAlive.push_back(std::move(layer));
        }
    }

    if (mallocTagsEnabled) {
        printf("MaxTotalBytes allocated: %zu\n",
               TfMallocTag::GetMaxTotalBytes());
        TfMallocTag::CallTree callTree;
        TfMallocTag::GetCallTree(&callTree);
        callTree.Report(std::cout);
    }
    
    return 0;
}
