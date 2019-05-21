/*
 * ranshelpers.h
 *
 *  Created on: May 21, 2019
 *      Author: Michael Lettrich (michael.lettrich@cern.ch)
 */

#pragma once

namespace rans {

template<typename T>
constexpr bool needs64Bit(){
	return sizeof(T)>4;
}

}  // namespace rans


