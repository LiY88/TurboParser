// Copyright (c) 2012-2015 Andre Martins
// All Rights Reserved.
//
// This file is part of TurboParser 2.3.
//
// TurboParser 2.3 is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TurboParser 2.3 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with TurboParser 2.3.  If not, see <http://www.gnu.org/licenses/>.

#ifndef PARAMETERS_H_
#define PARAMETERS_H_

#include "Features.h"
#include "SparseParameterVector.h"
#include "SparseLabeledParameterVector.h"
#include "Utils.h"

#if USE_WEIGHT_CACHING == 1
// Structure to define a feature-label pair.
struct FeatureLabelPair {
  uint64_t feature;
  int label;
};

// Structure to define a hash function and a comparison function for FeatureLabelPair.
struct FeatureLabelPairMapper {
  template <typename TSeed>
  inline void HashCombine(TSeed value, TSeed *seed) const {
    *seed ^= value + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
  }
  // Hash function.
  inline size_t operator()(const FeatureLabelPair& p) const {
    size_t hash = std::hash<uint64_t>()(p.feature);
    size_t hash_2 = std::hash<int>()(p.label);

    HashCombine<size_t>(hash_2, &hash);
    return hash;
  }
  // Comparison function.
  inline bool operator()(const FeatureLabelPair &p, const FeatureLabelPair &q) const {
    return p.feature == q.feature && p.label == q.label;
  }
};

// Defines a hash-table of FeatureLabelPair keys with values, of double type.
typedef std::unordered_map<FeatureLabelPair, double, FeatureLabelPairMapper, FeatureLabelPairMapper > FeatureLabelPairHashMap;

// Hash-table for caching FeatureLabelPair keys with corresponding values.
class FeatureLabelCache {
public:
  FeatureLabelCache() {
    hits_ = 0;
    misses_ = 0;
  };
  virtual ~FeatureLabelCache() {};

  int hits() const { return hits_; };
  int misses() const { return misses_; };
  int GetSize() const { return cache_.size(); };

  void IncrementHits() { hits_ += 1; };
  void IncrementMisses() { misses_ += 1; };

  // Insert a new pair {key, value} in the hash-table.
  void Insert(FeatureLabelPair key, double value) {
    cache_.insert({ key, value });
  };

  // Searches for a given key in the hash-table.
  // If found, value is returned in argument 'value'.
  // return: true if found, false otherwise.
  bool Find(FeatureLabelPair key, double * value) {
    FeatureLabelPairHashMap::const_iterator caching_iterator;
    caching_iterator = cache_.find(key);
    if (caching_iterator != cache_.end()) {
      *value = caching_iterator->second;
      return true;
    };
    return false;
  };

protected:
  FeatureLabelPairHashMap cache_;
  uint64_t hits_;
  uint64_t misses_;
};
#endif

// This class implements a feature vector, which is convenient to sum over
// binary features, weight them, etc. It just uses the classes
// SparseParameterVector and SparseLabeledParameterVector, which allow fast
// insertions and lookups.
class FeatureVector {
public:
  FeatureVector() {
    weights_.Initialize();
    labeled_weights_.Initialize();
  }
  virtual ~FeatureVector() {};
  const SparseParameterVectorDouble &weights() { return weights_; }
  const SparseLabeledParameterVector &labeled_weights() {
    return labeled_weights_;
  }
  SparseParameterVectorDouble *mutable_weights() { return &weights_; }
  SparseLabeledParameterVector *mutable_labeled_weights() {
    return &labeled_weights_;
  }
  double GetSquaredNorm() const {
    return weights_.GetSquaredNorm() + labeled_weights_.GetSquaredNorm();
  }

protected:
  SparseParameterVectorDouble weights_;
  SparseLabeledParameterVector labeled_weights_;
};

// This class handles the model parameters.
// It contains both "labeled" weights (for features that are conjoined with
// output labels) and regular weights.
// It allows averaging the parameters (as in averaged perceptron), which
// requires keeping around another weight vector of the same size.
class Parameters {
public:
  Parameters() {
    use_average_ = true;
  };
  virtual ~Parameters() {};

  // Save/load the parameters.
  void Save(FILE *fs);
  void Load(FILE *fs);

  // Initialize the parameters.
  void Initialize(bool use_average) {
    use_average_ = use_average;
    weights_.Initialize();
    if (use_average_) averaged_weights_.Initialize();
    labeled_weights_.Initialize();
    if (use_average_) averaged_labeled_weights_.Initialize();
  }

  // Lock/unlock the parameter vector. A locked vector means that no features
  // can be added.
  void StopGrowth() {
    weights_.StopGrowth();
    averaged_weights_.StopGrowth();
    labeled_weights_.StopGrowth();
    averaged_labeled_weights_.StopGrowth();
  }
  void AllowGrowth() {
    weights_.AllowGrowth();
    averaged_weights_.AllowGrowth();
    labeled_weights_.AllowGrowth();
    averaged_labeled_weights_.AllowGrowth();
  }

  // Get the number of parameters.
  // NOTE: this counts the parameters of the features that are conjoined with
  // output labels as a single parameter.
  int Size() const { return weights_.Size() + labeled_weights_.Size(); }

  // Checks if a feature exists.
  bool Exists(uint64_t key) const { return weights_.Exists(key); }

  // Checks if a labeled feature exists.
  bool ExistsLabeled(uint64_t key) const {
    return labeled_weights_.Exists(key);
  }

  // Get the weight of a "simple" feature.
  double Get(uint64_t key) const { return weights_.Get(key); }

  // Get the weights of features conjoined with output labels.
  // The vector "labels" contains the labels that we want to conjoin with;
  // label_scores will contain (as output) the weight for each label.
  // Return false if the feature does not exist, in which case the label_scores
  // will be an empty vector.
  bool Get(uint64_t key,
           const vector<int> &labels,
           vector<double> *label_scores) const {
    return labeled_weights_.Get(key, labels, label_scores);
  }

  // Get the squared norm of the parameter vector.
  double GetSquaredNorm() const {
    return weights_.GetSquaredNorm() + labeled_weights_.GetSquaredNorm();
  }

  // Compute the score corresponding to a set of "simple" features.
  double ComputeScore(const BinaryFeatures &features) const {
    double score = 0.0;
    for (int j = 0; j < features.size(); ++j) {
      score += Get(features[j]);
    }
    return score;
  }

  // Compute the scores corresponding to a set of features, conjoined with
  // output labels. The vector scores, provided as output, contains the score
  // for each label.
  void ComputeLabelScores(const BinaryFeatures &features,
                          const vector<int> &labels,
                          vector<double> *scores) const {
    scores->clear();
    scores->resize(labels.size(), 0.0);
    vector<double> label_scores(labels.size(), 0.0);
    for (int j = 0; j < features.size(); ++j) {
      if (!Get(features[j], labels, &label_scores)) continue;
      for (int k = 0; k < labels.size(); ++k) {
        (*scores)[k] += label_scores[k];
      }
    }
  }

#if USE_WEIGHT_CACHING == 1
  // Compute the scores corresponding to a set of features, conjoined with
  // output labels. The vector scores, provided as output, contains the score
  // for each label, with the added functionality
  // of using a cache for already computed scores.
  void ComputeLabelScoresWithCache(const BinaryFeatures &features,
                                   const vector<int> &labels,
                                   vector<double> *scores) {
    FeatureLabelPair caching_key;
    double caching_value;

    vector<int> reduced_labels;
    vector<int> adjust_new_index_reduced_labels;

    scores->clear();
    scores->resize(labels.size(), 0.0);
    vector<double> label_scores(labels.size(), 0.0);
    for (int j = 0; j < features.size(); ++j) {
      if (!ExistsLabeled(features[j])) continue;
      reduced_labels.clear();
      adjust_new_index_reduced_labels.clear();

      for (int k = 0; k < labels.size(); ++k) {
        caching_key = { features[j], labels[k] };
        if (!caching_weights_.Find(caching_key, &caching_value)) {
          // Add such label to reduced labels.
          reduced_labels.push_back(labels[k]);
          adjust_new_index_reduced_labels.push_back(k);
          caching_weights_.IncrementMisses();
        } else {
          (*scores)[k] += caching_value;
          caching_weights_.IncrementHits();
        }
      }
      if (reduced_labels.size() == 0) continue;
      if (!Get(features[j], reduced_labels, &label_scores)) continue;
      for (int k = 0; k < reduced_labels.size(); ++k) {
        (*scores)[adjust_new_index_reduced_labels[k]] += label_scores[k];

        caching_key = { features[j], reduced_labels[k] };
        caching_value = label_scores[k];
        caching_weights_.Insert(caching_key, caching_value);
      }
    }
  }
#endif

  // Scale the parameter vector by scale_factor.
  void Scale(double scale_factor) {
    weights_.Scale(scale_factor);
    labeled_weights_.Scale(scale_factor);
  }

  // Make a gradient step with a stepsize of eta, with respect to a vector
  // of "simple" features.
  // The iteration number is provided as input since it is necessary to
  // update the wanna-be "averaged parameters" in an efficient manner.
  void MakeGradientStep(const BinaryFeatures &features,
                        double eta,
                        int iteration,
                        double gradient) {
    for (int j = 0; j < features.size(); ++j) {
      weights_.Add(features[j], -eta * gradient);
      if (use_average_) {
        // perceptron/mira:
        // T*u1 + (T-1)*u2 + ... u_T = T*(u1 + u2 + ...) - u2 - 2*u3 - (T-1)*u_T
        // = T*w_T - u2 - 2*u3 - (T-1)*u_T
        averaged_weights_.Add(features[j],
                              static_cast<double>(iteration) * eta * gradient);
      }
    }
  }

  // Make a gradient step with a stepsize of eta, with respect to a vector
  // of features conjoined with a label.
  // The iteration number is provided as input since it is necessary to
  // update the wanna-be "averaged parameters" in an efficient manner.
  void MakeLabelGradientStep(const BinaryFeatures &features,
                             double eta,
                             int iteration,
                             int label,
                             double gradient) {
    for (int j = 0; j < features.size(); ++j) {
      labeled_weights_.Add(features[j], label, -eta * gradient);
    }

    if (use_average_) {
      for (int j = 0; j < features.size(); ++j) {
        averaged_labeled_weights_.Add(features[j], label,
                                      static_cast<double>(iteration) * eta * gradient);
      }
    }
  }

  // Finalize training, after a total of num_iterations. This is a no-op unless
  // we are averaging the parameter vector, in which case the averaged
  // parameters are finally computed and replace the original parameters.
  void Finalize(int num_iterations) {
    if (use_average_) {
      LOG(INFO) << "Averaging the weights...";

      averaged_weights_.Scale(1.0 / static_cast<double>(num_iterations));
      weights_.Add(averaged_weights_);

      averaged_labeled_weights_.Scale(
        1.0 / static_cast<double>(num_iterations));

      labeled_weights_.Add(averaged_labeled_weights_);
    }
  }

#if USE_WEIGHT_CACHING == 1
  int GetCachingWeightsHits()   const { return caching_weights_.hits(); };
  int GetCachingWeightsMisses() const { return caching_weights_.misses(); };
  int GetCachingWeightsSize()   const { return caching_weights_.GetSize(); };
#endif

protected:
  // Average the parameters as in averaged perceptron.
  bool use_average_;

  // Weights and averaged weights for the "simple" features.
  SparseParameterVectorDouble weights_;
  SparseParameterVectorDouble averaged_weights_;

  // Weights and averaged weights for the "labeled" features.
  SparseLabeledParameterVector labeled_weights_;
  SparseLabeledParameterVector averaged_labeled_weights_;

public:
#if USE_WEIGHT_CACHING == 1
  // Caches the weights for feature-label pairs :
  // FeatureLabelPair = struct {feature; label} .
  FeatureLabelCache caching_weights_;
#endif
};

#endif /*PARAMETERS_H_*/
