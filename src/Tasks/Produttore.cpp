/*
* malamore
Copyright (C) 2026 Giacomo Bergami

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */
#include <malamore.h>
#include <Library/KernelBinderClient.h>
#include <Library/KernelBufferChannel.h>

extern "C" __declspec(dllexport) void ExecuteCustomTask(void* pParcel, void* pIn, void* pOut) {
    BinderParcel* parcel = reinterpret_cast<BinderParcel*>(pParcel);
    
    // Legge il valore numerico fornito dal client dal parcel
    UCHAR clientInput = parcel->PayloadData[0];
    
    // Calcolo a basso livello
    UCHAR processedVal = clientInput + 10; 

    if (pOut) {
        KernelBufferChannel outputChannel(pOut);
        // Scrittura nello stream stile I/O C inter-processo
        outputChannel.Write(&processedVal, 1);
    }
}