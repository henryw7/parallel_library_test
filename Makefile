
nothing:
	@echo "make target includes: icc-cilk, icc-omp, gcc-omp, icx-omp"

icc-cilk:
	icpc queue.cpp -DUSE_CILK=1 -o $@.exe
	time ./$@.exe

icc-omp:
	icpc queue.cpp -fopenmp -o $@.exe
	time ./$@.exe

gcc-omp:
	g++ queue.cpp -fopenmp -o $@.exe
	time ./$@.exe

icx-omp:
	icpx queue.cpp -fopenmp -o $@.exe
	time ./$@.exe

clean:
	rm *.exe
