// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FIRST_PARTY_SETS_LOCAL_SET_DECLARATION_H_
#define NET_FIRST_PARTY_SETS_LOCAL_SET_DECLARATION_H_

#include <optional>

#include "base/containers/flat_map.h"
#include "net/base/net_export.h"
#include "net/base/schemeful_site.h"
#include "net/first_party_sets/first_party_set_entry.h"
#include "net/first_party_sets/sets_mutation.h"

namespace net {

// LocalSetDeclaration represents a Related Website Set that was defined locally
// by a web developer (as opposed to being one of the public Related Website
// Sets, or a set from the RelatedWebsiteSetsOverrides policy).
//
// Locally-defined sets take precedence over the public sets (for testing
// purposes), but can be overridden by the RelatedWebsiteSetsOverrides policy.
//
// See `GlobalFirstPartySets` for how overlaps/shadowing between public sets,
// locally-defined sets, and enterprise policy sets is handled.
class NET_EXPORT LocalSetDeclaration {
 public:
  // Constructs an empty (no-op) set declaration.
  LocalSetDeclaration();

  // Constructs a set declaration with the given entries. All entries must be in
  // the same set (i.e. they must have the same primary site). The set must not
  // be a singleton (i.e. must have more than one entry, or must be empty).
  //
  // Every alias must satisfy the following conditions:
  // * It must map to some canonical site that is in `set_entries`.
  // * If the alias is also present in `set_entries`, its entry must be
  // identical to the canonical's entry.
  //
  // Returns std::nullopt if any invariants are violated.
  static std::optional<LocalSetDeclaration> Create(
      base::flat_map<SchemefulSite, FirstPartySetEntry> set_entries,
      base::flat_map<SchemefulSite, SchemefulSite> aliases,
      bool emit_errors = false);

  ~LocalSetDeclaration();

  LocalSetDeclaration(const LocalSetDeclaration&);
  LocalSetDeclaration& operator=(const LocalSetDeclaration&);
  LocalSetDeclaration(LocalSetDeclaration&&);
  LocalSetDeclaration& operator=(LocalSetDeclaration&&);

  bool empty() const { return entries_.empty(); }

  size_t size() const { return entries_.size() + aliases_.size(); }

  // Computes the SetsMutation that can be used to implement the changes
  // described in this instance.
  SetsMutation ComputeMutation() const;

 private:
  LocalSetDeclaration(
      base::flat_map<SchemefulSite, FirstPartySetEntry> set_entries,
      base::flat_map<SchemefulSite, SchemefulSite> aliases);

  // Stores the set of entries, without ccTLD aliases. This may be empty if no
  // set was locally defined.
  base::flat_map<SchemefulSite, FirstPartySetEntry> entries_;

  // Stores the ccTLD aliases. May be empty.
  base::flat_map<SchemefulSite, SchemefulSite> aliases_;
};

}  // namespace net

#endif  // NET_FIRST_PARTY_SETS_LOCAL_SET_DECLARATION_H_
