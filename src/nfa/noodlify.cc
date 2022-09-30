/* noodlify.cc -- Noodlification of NFAs
 *
 * Copyright (c) 2018 Ondrej Lengal <ondra.lengal@gmail.com>
 *
 * This file is a part of libmata.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <iostream>
#include <bitset>

#include <mata/nfa.hh>
#include <mata/noodlify.hh>
#include <mata/util.hh>

using namespace Mata::Nfa;

namespace
{

/**
 * Get a number of permutations for computed epsilon depths.
 * @param[in] epsilon_depths Computed list of epsilon transitions for each depth.
 * @return Number of permutations.
 */
size_t get_num_of_permutations(const SegNfa::Segmentation::EpsilonDepthTransitions& epsilon_depths)
{
    size_t num_of_permutations{ 1 };
    for (const auto& segment: epsilon_depths)
    {
        num_of_permutations *= segment.second.size();
    }
    return num_of_permutations;
}

} // namespace

SegNfa::NoodleSequence SegNfa::noodlify(const SegNfa& aut, const Symbol epsilon, std::vector<std::vector<size_t>> variableLocations, EnumAlphabet &alph, bool include_empty, bool useBits) {
    Segmentation segmentation{ aut, epsilon };
    const auto& segments{ segmentation.get_segments_raw() };

    if (segments.size() == 1) {
        std::shared_ptr<Nfa> segment = std::make_shared<Nfa>(segments[0]);
        segment->trim();
        if (segment->get_num_of_states() > 0 || include_empty) {
            return {{ segment }};
        } else {
            return {};
        }
    }

    State unused_state = aut.get_num_of_states(); // get some State not used in aut

    // segments_one_initial_final[init, final] is the pointer to automaton created from one of
    // the segments such that init and final are one of the initial and final states of the segment
    // and the created automaton takes this segment, sets initialstates={init}, finalstates={final}
    // and trims it; also segments_one_initial_final[unused_state, final] is used for the first
    // segment (where we always want all initial states, only final state changes) and
    // segments_one_initial_final[init, unused_state] is similarly for the last segment
    // TODO: should we use unordered_map? then we need hash
    std::map<std::pair<State, State>, std::shared_ptr<Nfa>> segments_one_initial_final;

    // TODO this could probably be written better
    for (auto iter = segments.begin(); iter != segments.end(); ++iter) {
        if (iter == segments.begin()) { // first segment will always have all initial states in noodles
            for (const State final_state: iter->finalstates) {
                SharedPtrAut segment_one_final = std::make_shared<Nfa>(*iter);
                segment_one_final->finalstates = { final_state };
                segment_one_final->trim();

                if (segment_one_final->get_num_of_states() > 0 || include_empty) {
                    segments_one_initial_final[std::make_pair(unused_state, final_state)] = segment_one_final;
                }
            }
        } else if (iter + 1 == segments.end()) { // last segment will always have all final states in noodles
            for (const State init_state: iter->initialstates) {
                SharedPtrAut segment_one_init = std::make_shared<Nfa>(*iter);
                segment_one_init->initialstates = { init_state };
                segment_one_init->trim();

                if (segment_one_init->get_num_of_states() > 0 || include_empty) {
                    segments_one_initial_final[std::make_pair(init_state, unused_state)] = segment_one_init;
                }
            }
        } else { // the segments in-between
            for (const State init_state: iter->initialstates) {
                for (const State final_state: iter->finalstates) {
                    SharedPtrAut segment_one_init_final = std::make_shared<Nfa>(*iter);
                    segment_one_init_final->initialstates = { init_state };
                    segment_one_init_final->finalstates = { final_state };
                    segment_one_init_final->trim();

                    if (segment_one_init_final->get_num_of_states() > 0 || include_empty) {
                        segments_one_initial_final[std::make_pair(init_state, final_state)] = segment_one_init_final;
                    }
                }
            }
        }
    }

    ///// STUFF FOR PRINTING AFA ///////

    //aut.print_to_DOT(std::cout);
    std::ostream &afaOutput = std::cerr;

    if (!useBits) {
        afaOutput << "@AFA-explicit" << std::endl
                //<< "%Alphabet-chars" << std::endl
                << "%Alphabet-numbers" << std::endl
                << "%Tracks-auto" << std::endl;
    } else {
        afaOutput << "@AFA-bits" << std::endl;
    }

    afaOutput << "%Initial (";
    bool isThisFirstState = true;
    for (State init : aut.initialstates) {
        if (isThisFirstState) {
            isThisFirstState = false;
        } else {
            afaOutput << " | ";
        }
        afaOutput << "q" << init;
    }
    afaOutput << ")";

    std::vector<StateSet> segmentReachableStates = {segments[0].get_reachable_states()};
    std::vector<std::vector<State>> initialStatesNoodles = { std::vector<State>{} };
    std::vector<State> allSegmentsInitialStates;
    std::unordered_map<State, StateSet> initStateToPreviousNonFinalStates;
    for (auto iter = segments.begin() + 1; iter != segments.end(); ++iter) {

        std::vector<std::vector<State>> oldInitialStatesNoodles = initialStatesNoodles;
        initialStatesNoodles = std::vector<std::vector<State>>{};
        for (State init : iter->initialstates) {
            initStateToPreviousNonFinalStates[init] = segmentReachableStates.back();
            allSegmentsInitialStates.push_back(init);
            for (std::vector<State> oldNoodle : oldInitialStatesNoodles) {
                oldNoodle.push_back(init);
                initialStatesNoodles.push_back(oldNoodle);
            }
        }

        segmentReachableStates.push_back(iter->get_reachable_states());
    }

    afaOutput << " & (";
    bool isFirstNoodle = true;
    for (const auto &initialStatesNoodle : initialStatesNoodles) {
        if (isFirstNoodle) {
            isFirstNoodle = false;
        } else {
            afaOutput << " | ";
        }

        afaOutput << "(";
        bool isFirstInitState = true;
        for (State init : initialStatesNoodle) {
            if (isFirstInitState) {
                isFirstInitState = false;
            } else {
                afaOutput << " & ";
            }
            afaOutput << "q" << init << " & " << "q" << init << "'";
        }
        afaOutput << ")";
    }
    afaOutput << ")" << std::endl;

    for (const auto &elem : segmentation.get_epsilon_depths()) {
        for (const auto &tran : elem.second) {
            initStateToPreviousNonFinalStates[tran.tgt].remove(tran.src);
        }
    }

    auto stateSetMinus = [](const StateSet &lhs, const StateSet &rhs) {
        StateSet ret;
        for (State s : lhs) {
            if (rhs.count(s) == 0) {
                ret.push_back(s);
            }
        }
        return ret;
    };

    afaOutput << "%Final ";
    const StateSet finalSegNonFinalStates = stateSetMinus(segmentReachableStates.back(), segments.back().finalstates);
    if (finalSegNonFinalStates.size() == 0) {
        afaOutput << "true";
    } else {
        afaOutput << "(";
        bool isThisFirstState = true;
        for (State nonFinal : finalSegNonFinalStates) {
            if (isThisFirstState) {
                isThisFirstState = false;
            } else {
                afaOutput << " & ";
            }
            afaOutput << "!q" << nonFinal;
        }
        afaOutput << ")";
    }

    for (const auto &elem : initStateToPreviousNonFinalStates) {
        if (elem.second.size() == 0) {
            continue;
        }
    //for (State init : allSegmentsInitialStates) {
        afaOutput << " & (!q" << elem.first << "' | (";
        bool isFirst = true;
        for (State s : elem.second) {
            if (isFirst) {
                isFirst = false;
            } else {
                afaOutput << " & ";
            }
            afaOutput << "!q" << s;
        }
        afaOutput << "))";
    }
    afaOutput << std::endl;

    std::unordered_map<Symbol, Symbol> symbolRemapping;
    Symbol newSymbol = 0;
    size_t neededBits = 1;
    if (alph.get_symbols().size() > 1) {
        neededBits = 32 - __builtin_clz(alph.get_symbols().size() - 1);
    }
    auto remapSymbol = [&symbolRemapping, &newSymbol, &useBits, neededBits](Symbol symToRemap, size_t trackNum) {
        auto it = symbolRemapping.find(symToRemap);
        Symbol remappedSymbol;
        if (it == symbolRemapping.end()) {
            symbolRemapping[symToRemap] = newSymbol;
            remappedSymbol = newSymbol++;
        } else {
            remappedSymbol = it->second;
        }

        std::stringstream result;
        if (useBits) {
            std::string symBits = std::bitset<32>(remappedSymbol).to_string();
            
            size_t startingBit = trackNum*neededBits;
            for (size_t i = 0; i < neededBits; ++i) {
                if (i != 0) {
                    result << " & ";
                }
                if (symBits[31-i] == '0') {
                    result << "!";
                }
                result << "a" << startingBit + i;
            }
        } else {
            result << remappedSymbol << "@t" << trackNum;
        }
        return result.str();
    };

    auto numOfVars = variableLocations.size();
    for (size_t varNum = 0; varNum < numOfVars; ++varNum) {
        for (auto varLoc : variableLocations[varNum]) {
            const Nfa &varAut = segments[varLoc];
            for (State s : segmentReachableStates[varLoc]) {
                const auto &transFromState = varAut.get_transitions_from(s);
                if (transFromState.size() == 0) {
                    continue; // we do not print transitions from s, as there are no transitions to print
                }
                afaOutput << "q" << s << " ";
                bool isFirstTran = true;
                for (const TransSymbolStates &tran : transFromState) {
                    if (isFirstTran) {
                        isFirstTran = false;
                    } else {
                        afaOutput << " | ";
                    }
                    //afaOutput << "(" << (char) tran.symbol << "@t" << varNum << " & (";
                    afaOutput << "(" << remapSymbol(tran.symbol, varNum) << " & (";
                    bool isFirstStateTo = true;
                    for (State r : tran.states_to) {
                        if (isFirstStateTo) {
                            afaOutput << "q" << r;
                            isFirstStateTo = false;
                        } else {
                            afaOutput << " | q" << r;
                        }
                        
                    }
                    afaOutput << "))";
                }
                afaOutput << std::endl;
            }
        }
    }

    for (State init : allSegmentsInitialStates) {
        afaOutput << "q" << init << "' " << "q" << init << "'" << std::endl;
    }

    afaOutput << "#AFA was fully printed" << std::endl;

    ///// END STUFF FOR PRINTING AFA ///////

    const auto& epsilon_depths{ segmentation.get_epsilon_depths() };

    // Compute number of all combinations of ε-transitions with one ε-transitions from each depth.
    size_t num_of_permutations{ get_num_of_permutations(epsilon_depths) };
    size_t epsilon_depths_size{ epsilon_depths.size() };

    NoodleSequence noodles{};
    // noodle of epsilon transitions (each from different depth)
    TransSequence epsilon_noodle(epsilon_depths_size);
    // for each combination of ε-transitions, create the automaton.
    // based on https://stackoverflow.com/questions/48270565/create-all-possible-combinations-of-multiple-vectors
    for (size_t index{ 0 }; index < num_of_permutations; ++index) {
        size_t temp{ index };
        for (size_t depth{ 0 }; depth < epsilon_depths_size; ++depth) {
            size_t num_of_trans_at_cur_depth = epsilon_depths.at(depth).size();
            size_t computed_index = temp % num_of_trans_at_cur_depth;
            temp /= num_of_trans_at_cur_depth;
            epsilon_noodle[depth] = epsilon_depths.at(depth)[computed_index];
        }

        Noodle noodle;

        // epsilon_noodle[0] for sure exists, as we sorted out the case of only one segment at the beginning
        auto first_segment_iter = segments_one_initial_final.find(std::make_pair(unused_state, epsilon_noodle[0].src));
        if (first_segment_iter != segments_one_initial_final.end()) {
            noodle.push_back(first_segment_iter->second);
        } else {
            continue;
        }

        bool all_segments_exist = true;
        for (auto iter = epsilon_noodle.begin(); iter + 1 != epsilon_noodle.end(); ++iter) {
            auto next_iter = iter + 1;
            auto segment_iter = segments_one_initial_final.find(std::make_pair(iter->tgt, next_iter->src));
            if (segment_iter != segments_one_initial_final.end()) {
                noodle.push_back(segment_iter->second);
            } else {
                all_segments_exist = false;
                break;
            }
        }

        if (!all_segments_exist) {
            continue;
        }

        auto last_segment_iter = segments_one_initial_final.find(
                std::make_pair(epsilon_noodle.back().tgt, unused_state));
        if (last_segment_iter != segments_one_initial_final.end()) {
            noodle.push_back(last_segment_iter->second);
        } else {
            continue;
        }

        noodles.push_back(noodle);
    }
    return noodles;
}

SegNfa::NoodleSequence SegNfa::noodlify_for_equation(const AutRefSequence& left_automata, const Nfa& right_automaton,
std::vector<std::vector<size_t>> variableLocations,
                                                     bool include_empty, const StringDict& params) {
    const auto left_automata_begin{ left_automata.begin() };
    const auto left_automata_end{ left_automata.end() };
    for (auto left_aut_iter{ left_automata_begin }; left_aut_iter != left_automata_end;
         ++left_aut_iter) {
        (*left_aut_iter).get().unify_initial();
        (*left_aut_iter).get().unify_final();
    }

    if (left_automata.empty() || is_lang_empty(right_automaton)) { return NoodleSequence{}; }

    auto alphabet{ EnumAlphabet::from_nfas(left_automata) };
    alphabet.add_symbols_from(right_automaton);
    const Symbol epsilon{ alphabet.get_next_value() };

    // Automaton representing the left side concatenated over epsilon transitions.
    Nfa concatenated_left_side{ *left_automata_begin };
    for (auto next_left_automaton_it{ left_automata_begin + 1 }; next_left_automaton_it != left_automata_end;
         ++next_left_automaton_it) {
        concatenated_left_side = concatenate(concatenated_left_side, *next_left_automaton_it, epsilon);
    }

    auto product_pres_eps_trans{ intersection(concatenated_left_side, right_automaton, epsilon) };
    product_pres_eps_trans.trim();
    if (is_lang_empty(product_pres_eps_trans)) {
        return NoodleSequence{};
    }
    if (util::haskey(params, "reduce")) {
        const std::string& reduce_value = params.at("reduce");
        if (reduce_value == "forward" || reduce_value == "bidirectional") {
            product_pres_eps_trans = reduce(product_pres_eps_trans);
        }
        if (reduce_value == "backward" || reduce_value == "bidirectional") {
            product_pres_eps_trans = invert(product_pres_eps_trans);
            product_pres_eps_trans = reduce(product_pres_eps_trans);
            product_pres_eps_trans = invert(product_pres_eps_trans);
        }
    }
    bool useBits = true;
    if (util::haskey(params, "afa-type")) {
        const std::string& type_value = params.at("afa-type");
        if (type_value == "bits") {
            useBits = true;
        }
        if (type_value == "tracks") {
            useBits = false;
        }
    }
    return noodlify(product_pres_eps_trans, epsilon, variableLocations, alphabet, include_empty, useBits);
}

SegNfa::NoodleSequence SegNfa::noodlify_for_equation(const AutPtrSequence& left_automata, const Nfa& right_automaton,
                                                     std::vector<std::vector<size_t>> variableLocations,
                                                     bool include_empty, const StringDict& params
                                                     ) {
    const auto left_automata_begin{ left_automata.begin() };
    const auto left_automata_end{ left_automata.end() };

    std::string reduce_value{};
    if (util::haskey(params, "reduce")) {
        reduce_value = params.at("reduce");
    }

    if (!reduce_value.empty()) {
        if (reduce_value == "forward" || reduce_value == "backward" || reduce_value == "bidirectional") {
            for (auto left_aut_iter{ left_automata_begin }; left_aut_iter != left_automata_end;
                 ++left_aut_iter) {
                (*left_aut_iter)->unify_initial();
                (*left_aut_iter)->unify_final();
            }
        }
    }

    if (left_automata.empty() || is_lang_empty(right_automaton)) { return NoodleSequence{}; }

    auto alphabet{ EnumAlphabet::from_nfas(left_automata) };
    alphabet.add_symbols_from(right_automaton);
    const Symbol epsilon{ alphabet.get_next_value() };

    // Automaton representing the left side concatenated over epsilon transitions.
    Nfa concatenated_left_side{ *(*left_automata_begin) };
    for (auto next_left_automaton_it{ left_automata_begin + 1 }; next_left_automaton_it != left_automata_end;
         ++next_left_automaton_it) {
        concatenated_left_side = concatenate(concatenated_left_side, *(*next_left_automaton_it), epsilon);
    }

    auto product_pres_eps_trans{ intersection(concatenated_left_side, right_automaton, epsilon) };
    product_pres_eps_trans.trim();
    if (is_lang_empty(product_pres_eps_trans)) {
        return NoodleSequence{};
    }
    if (!reduce_value.empty()) {
        if (reduce_value == "forward" || reduce_value == "bidirectional") {
            product_pres_eps_trans = reduce(product_pres_eps_trans);
        }
        if (reduce_value == "backward" || reduce_value == "bidirectional") {
            product_pres_eps_trans = invert(product_pres_eps_trans);
            product_pres_eps_trans = reduce(product_pres_eps_trans);
            product_pres_eps_trans = invert(product_pres_eps_trans);
        }
    }
    bool useBits = true;
    if (util::haskey(params, "afa-type")) {
        const std::string& type_value = params.at("afa-type");
        if (type_value == "bits") {
            useBits = true;
        }
        if (type_value == "tracks") {
            useBits = false;
        }
    }
    return noodlify(product_pres_eps_trans, epsilon, variableLocations, alphabet, include_empty, useBits);
}
