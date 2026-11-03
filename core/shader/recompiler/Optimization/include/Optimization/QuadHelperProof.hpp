#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_QUADHELPERPROOF_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_QUADHELPERPROOF_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <span>
#include <string>

namespace ShaderRecompiler {

// A program that runs one lane per invocation reads its own bit of WqmU64(ballot(predicate)) through `reads`
// (INotEqual32 own-lane bit tests of `wqm`). That bit differs from `predicate` only on quad-helper lanes: lanes
// whose own test is false while a lane of their quad passes it. Returns an empty string when replacing every read
// with `predicate` is proved to change nothing such a lane writes, exports, hands to another lane or leaves live,
// and otherwise the reason the proof failed.
//
// The proof runs the lane twice side by side from the WqmU64, once with the bit set (console) and once with it clear
// (lowered), on symbolic values: values both runs compute from the same inputs are equal, and a run's branches are
// followed both ways when they are not known. It succeeds when every path reaches a point where no value that differs
// between the runs is still live, without either run doing anything another lane or memory can see with a value that
// differs, and without the runs taking different paths into another iteration of a loop.
[[nodiscard]] std::string ProveQuadHelpersUnobserved(const IrProgram& program, const IrValue& wqm, std::span<IrValue* const> reads, const IrValue& predicate);

}

#endif
