// Copyright Noah Games, Inc. All Rights Reserved.

/* This file contains utility functions for meta programming
 *
 * Since you're in here you're probably quite observant, and you'll
 * note that it's quite barren here. This is because template
 * metaprogramming is awful and I try not to engage in it. However,
 * sometimes these things are forced upon us.
 *
 */

namespace zen {

/**
 * Uses implicit conversion to create an instance of a specific type.
 * Useful to make things clearer or circumvent unintended type deduction in templates.
 * Safer than C casts and static_casts, e.g. does not allow down-casts
 *
 * @param Obj  The object (usually pointer or reference) to convert.
 *
 * @return The object converted to the specified type.
 */
template<typename T>
inline T
ImplicitConv(typename std::type_identity<T>::type Obj)
{
	return Obj;
}

}  // namespace zen

