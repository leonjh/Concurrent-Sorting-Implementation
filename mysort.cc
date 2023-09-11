#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>

#define MAX_LINE_LENGTH 4096 // Used for initializing char arrays which read in the numbers from the files

/**
 * @brief A struct which allows for the threads to communicate between themselves. Note that although
 * when it comes to threads there is no parent/child relationship, we use the terminology to make what
 * each set of arrays does more clear.
 * 
 * The parent_send_pipe array represents the pipe which exec_thread_sort will send data to that the thread will
 * read and sort.
 * 
 * The child_send_pipe array represents the pipe which the thread will send the sorted data back to exec_thread_sort 
 * to read and combine with other results, printing when finished.
 * 
 * Note ipc_pipe stands for Inter-Process-Communication Pipe
 * 
 */
typedef struct ipc_pipe_st {
    // Pipe for the 'parent' (exec_thread_sort) to write and 'child' (spawned thread) to read
    int parent_send_pipe[2];
    
    // Pipe for the 'child' (spawned thread) to write and 'parent' (exec_thread_sort) to read
    int child_send_pipe[2];
} ipc_pipe;

using namespace std;

/**
 * @brief Performs bubble sort on a passed in vector
 * 
 * @param arr - An long vector to be bubbled sorted
 */
void bubble_sort(vector<long>& arr) {
    for (int j = 0; j < arr.size() - 1; j++) {
        for (int i = 0; i < arr.size() - j - 1; i++) {
            if (arr[i] > arr[i+1]) {
                swap(arr[i], arr[i+1]);
            }
        }
    }
}

/**
 * @brief Merges two sorted vectors into one sorted vector of all the elements
 * 
 * @param vector_one - The first sorted vector
 * @param vector_two - The second sorted vector
 * @return vector<int> - The vector containing all of vector_one and vector_two's elements in sorted order
 */
vector<long> merge_vects(vector<long> vector_one, vector<long> vector_two) {
    // The vector of all the combined elements which will be returned
    vector<long> combined_vector;

    // Pointers to the current location in the vectors that are being combined
    int one_ptr = 0;
    int two_ptr = 0;
    while (one_ptr < vector_one.size() && two_ptr < vector_two.size()) {
        if (vector_one[one_ptr] < vector_two[two_ptr]) {
            combined_vector.push_back(vector_one[one_ptr]);
            one_ptr++;
        } else {
            combined_vector.push_back(vector_two[two_ptr]);
            two_ptr++;
        }
    }
    
    while (one_ptr < vector_one.size()) {
        combined_vector.push_back(vector_one[one_ptr++]);
    }

    while (two_ptr < vector_two.size()) {
        combined_vector.push_back(vector_two[two_ptr++]);
    }

    return combined_vector;
}

/**
 * @brief Loads in numbers from each file and directly sorts/prints them, using no concurrency.
 * 
 * @param file_names - A vector containing all the files with numbers within them.
 */
void exec_normal_sort(vector<string> &file_names) {
    // If there are no file names simply return
    if (file_names.size() == 0) {
        printf("No valid file names entered");
        return;
    }

    // Create the vector for all the numbers to go into
    vector<long> nums;

    for (int i = 0; i < file_names.size(); i++) {
        // Open each file in read mode
        FILE* cur_file = fopen(file_names[i].c_str(), "r");
        if (cur_file == NULL) {
            perror("Error opening file. Check if file exists / file permissions.");
            exit(EXIT_FAILURE);
        }

        // String holding in which line we read in & the number after conversion from string to long
        char line[MAX_LINE_LENGTH] = {0};
        long read_num;
        
        // Parse each line of the file, rotating which vector receives the number
        while (fgets(line, sizeof(line), cur_file)) {
            read_num = strtol(line, NULL, 10);
            nums.push_back(read_num);
        }

        fclose(cur_file);
    }

    // Sort all of the numbers
    bubble_sort(nums);

    // Print the sorted numbers
    for (int i = 0; i < nums.size(); i++) {
        cout << nums[i] << endl;
    }
}

/**
 * @brief Sorts the input numbers using the user specified number of child processes printing
 * the final result.
 * 
 * @param num_children - The number of child processes to use
 * @param vects - A vector of vector<long>s, where each vector<long> contains the numbers a different
 * child will sort, to later be merged and then printed.
 */
void exec_process_sort(int num_children, vector<vector<long> > vects) { 
    // Pipes for each child process so that the parent can send data to the child
    int parent_send_pipe[num_children][2];

    // Pipes for each child process so that the child can send data to the parent
    int child_send_pipe[num_children][2];

    for (int i = 0; i < num_children; i++) {
        // Create the necessary pipes for the parent and child to communicate
        if (pipe(parent_send_pipe[i]) == -1) {
            perror("Error pipping");
            exit(EXIT_FAILURE);
        }
        if (pipe(child_send_pipe[i]) == -1) {
            perror("Error pipping");
            exit(EXIT_FAILURE);
        }
        
        pid_t pid;  
        pid = fork();
        if (pid == 0) { // Child process
            // Close the child_send read end and parent_send write end
            close(child_send_pipe[i][0]);
            close(parent_send_pipe[i][1]);

            // Read in all the values the parent sends to the child and add to to_sort
            vector<long> to_sort;
            long read_long;
            while(read(parent_send_pipe[i][0], &read_long, sizeof(read_long)) > 0) {
                to_sort.push_back(read_long);
            }

            // Sort the collected values
            bubble_sort(to_sort);

            // Write the sorted numbers back to the parent
            for (int j = 0; j < to_sort.size(); j++) {
                write(child_send_pipe[i][1], &to_sort[j], sizeof(long));
            }

            // Close the child_send write end
            close(child_send_pipe[i][1]);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) { // Parent Process
            // Close the child_send write end and the parent_send read end
            close(child_send_pipe[i][1]);
            close(parent_send_pipe[i][0]);

            // Write all of the unsorted values to the child
            for (int j = 0; j < vects[i].size(); j++) {
                write(parent_send_pipe[i][1], &vects[i][j], sizeof(long));
            }

            // Close the parent_send write end
            close(parent_send_pipe[i][1]);
        } else { // Failure
            perror("Error forking");
            exit(EXIT_FAILURE);
        }
    }

    // Read the sorted values from the child, adding them to vectors and merging with all_nums
    vector<long> all_nums;
    for (int i = 0; i < num_children; i++) {
        vector<long> read_nums;
        long read_long;
        while(read(child_send_pipe[i][0], &read_long, sizeof(long))) {
            read_nums.push_back(read_long);
        }

        // Merge all_nums and the newly created vector
        all_nums = merge_vects(all_nums, read_nums);

        // Close the child_send read end
        close(child_send_pipe[i][0]);
    }

    // Wait for each child process to terminate
    while (wait(NULL) > 0) { }

    // Print the sorted numbers
    for (int i = 0; i < all_nums.size(); i++) {
        cout << all_nums[i] << endl;
    }
}

/**
 * @brief The thread that is spawned to sort a parition of the numbers
 * 
 * @param arg - an ipc_pipe struct pointer, which contains the pipe information for the 
 * threads 'parent' thread as well as its own 'child' thread pipe information, so that the threads may communicate
 * @return void* - Nothing - the thread exits using pthread_exit(NULL)
 */
void* thread_sorter(void* arg) {
    // The ipc_pipe pointer containing the pipes needed for communicate
    ipc_pipe* pipes = (ipc_pipe*) arg;

    // Read all the data being sent by the parent process, and load it into a vector
    vector<long> read_nums;
    long read_long;
    while (read(pipes->parent_send_pipe[0], &read_long, sizeof(long)) > 0) {
        read_nums.push_back(read_long);
    }

    // Close the parent_send read end
    close(pipes->parent_send_pipe[0]);

    // Sort the collected numbers
    bubble_sort(read_nums);

    // Send the sorted numbers back to the exec_thread_sort
    for (int i = 0; i < read_nums.size(); i++) {
        write(pipes->child_send_pipe[1], &read_nums[i], sizeof(long));
    }

    // Close the child_send write end
    close(pipes->child_send_pipe[1]);

    pthread_exit(NULL);
}

/**
 * @brief Sorts the unput numers using the user specified number of threads, printing the final result.
 * 
 * @param num_threads  - The number of threads to use
 * @param vects - A vector of vector<long>s, where each vector<long> contains the numbers a different
 * thread will sort, which will later be merged into one vector and printed.
 */
void exec_thread_sort(int num_threads, vector<vector<long> > vects) {
    // All of the num_threads threads so that we can access them later
    pthread_t threads[num_threads];

    // All of the ipc_pipe structs so we can reference the pipe fd's later
    ipc_pipe thread_pipes[num_threads];

    for (int i = 0; i < num_threads; i++) {
        ipc_pipe cur_pipes;
        
        // Create the neccessary pipes within the cur_pipes ipc_pipe struct for the threads to communicate
        if (pipe(cur_pipes.parent_send_pipe) == -1) {
            perror("Error pipping");
            exit(EXIT_FAILURE);
        }
        if (pipe(cur_pipes.child_send_pipe) == -1) {
            perror("Error pipping");
            exit(EXIT_FAILURE);
        }
        thread_pipes[i] = cur_pipes;

        // Create new thread with its ipc pipe 
        pthread_create(&threads[i], NULL, thread_sorter, &thread_pipes[i]);

        // Send the data one by one - same method as above
        for (int j = 0; j < vects[i].size(); j++) {
            write(cur_pipes.parent_send_pipe[1], &vects[i][j], sizeof(long));
        }

        // Close exec_thread_sort's parent_send write end
        close (cur_pipes.parent_send_pipe[1]);
    }

    // Read the sorted values from the spawned thread, adding them to vectors and merging with all_nums
    vector<long> all_nums;
    for (int i = 0; i < num_threads; i++) {
        vector<long> read_nums;
        long read_long;
        while(read(thread_pipes[i].child_send_pipe[0], &read_long, sizeof(long)) > 0) {
            read_nums.push_back(read_long);
        }

        // Merge all_nums with the newly created vector
        all_nums = merge_vects(all_nums, read_nums);

        // Close the spawned thread's child_send read end
        close(thread_pipes[i].child_send_pipe[0]);
    }

    // pthread_join to make sure all threads complete corectly
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Print the sorted numbers
    for (int i = 0; i < all_nums.size(); i++) {
        cout << all_nums[i] << endl;
    }
}

/**
 * @brief Partitions all of the numbers evenly from each file into vectors to be sorted based off the numbers of threads/processes.
 * The numbers are partitioned evenly between the vectors for each thread/process
 * 
 * @param file_names A vector containing the names of every file that needs to be opened and parsed
 * @param partitioned_nums A vector of long vectors which has the numbers evenly distributed into them.
 * @param num_concurrency The number of threads/processes as to evenly distribute
 */
void create_vectors(vector<string> &file_names, vector<vector<long>> &partitioned_nums, int num_concurrency) {
    if (file_names.size() == 0) {
        printf("No valid file names entered");
        return;
    }

    // Create the vectors for each thread/process
    for (int i = 0; i < num_concurrency; i++) {
        vector<long> thread_vector;
        partitioned_nums.push_back(thread_vector);
    }

    // Number of which number we're currently reading in from the file. The vector the current number is added 
    // to is decided by doing cur_vector % num_concurrency, thus we distribute the numbers between each thread/process
    // evenly
    long cur_vector = 0;
    for (int i = 0; i < file_names.size(); i++) {
        // Open the file in read mode
        FILE* cur_file = fopen(file_names[i].c_str(), "r");
        if (cur_file == NULL) {
            perror("Error opening file. Check if file exists / file permissions.");
            exit(EXIT_FAILURE);
        }

        // String holding in which line we read in & the number after conversion from string to long
        char line[MAX_LINE_LENGTH] = {0};
        long read_num;
        
        // Parse each line of the file, rotating which vector receives the number
        while (fgets(line, sizeof(line), cur_file)) {
            read_num = strtol(line, NULL, 10);
            partitioned_nums[cur_vector % num_concurrency].push_back(read_num);
            cur_vector++;
        }

        fclose(cur_file);
    }
}

/**
 * @brief The main method which parses the parameters input to the program and invokes the correct form of sorting
 * 
 * @param argc - The count of arguments passed in
 * @param argv - A char array pointer to the arguments entered
 * @return int - EXIT_SUCCESS on successs
 */
int main(int argc, char *argv[]) {
    // Wether to use threads or processes
    bool use_threads = false; 

    // The number of threads/processes (depending on the mode) to use. Default is 4
    int num_concurrency = 4; 

    if (argc == 1) {
        fprintf(stderr, "Leon Hertzberg - leonjh");
        return EXIT_SUCCESS;
    }

    int opt;
    while ((opt = getopt(argc, argv, "tn:")) != -1) {
        switch (opt) {
            case 't':
                use_threads = true;
                break;
            case 'n':
                num_concurrency = atoi(optarg);
                if (num_concurrency < 0) { num_concurrency = 4; }
                break;
            case '?':
                printf("Use correct format: ./mysort (optional: -t) -n (number >= 0) <files>");
                exit(EXIT_SUCCESS);
            default:
                /* Add printing full name / login when no args are given*/
                fprintf(stderr, "Leon Hertzberg - leonjh");
                exit(EXIT_SUCCESS);
        }
    }

    // Add all of the entered file names to a vector to be loaded in
    vector<string> file_names;
    for(; optind < argc; optind++){  
        file_names.push_back(argv[optind]);   
    }

    // If the number of threads/processes is 0 or 1 simply sort the input
    if (num_concurrency <= 1) {
        exec_normal_sort(file_names);
        return EXIT_SUCCESS;
    }

    // Create a vector containing vectors of numbers each thread/process will sort
    vector<vector<long>> nums;
    create_vectors(file_names, nums, num_concurrency);

    // Call the correct sorting method
    if (use_threads) {
        exec_thread_sort(num_concurrency, nums);
    } else {
        exec_process_sort(num_concurrency, nums);
    }

    return EXIT_SUCCESS;
}