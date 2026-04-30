%% Φόρτωση δεδομένων
clear; clc; close all;

%Φορτώνουμε το C από το αρχικό αρχείο
load('input.mat', 'C');

%Φορτώνουμε τα αποτελέσματα του C προγράμματος (Pthreads)
load('output.mat', 'idx', 'dst');
idx_c = round(idx); % Σιγουρευόμαστε ότι είναι ακέραιοι
dst_c = dst;

%% Τρέχουμε την knnsearch του MATLAB για σύγκριση
k = size(idx_c, 2); 
[idx_matlab, dst_matlab] = knnsearch(C, C, 'K', k);

%% Οπτικοποίηση για ένα τυχαίο σημείο
% Ελιπογή σημείου
query_point_id = 3; 

figure('Name', 'k-NN Verification', 'NumberTitle', 'off');
hold on; grid on;

scatter(C(:,1), C(:,2), 20, [0.8 0.8 0.8], 'filled');

scatter(C(query_point_id, 1), C(query_point_id, 2), 150, 'r', 'p', 'filled');
text(C(query_point_id, 1)+2, C(query_point_id, 2), 'Query Point', 'Color', 'r', 'FontWeight', 'bold');

neighbors_c_ids = idx_c(query_point_id, :);
scatter(C(neighbors_c_ids, 1), C(neighbors_c_ids, 2), 80, 'b', 'o', 'LineWidth', 2);

neighbors_m_ids = idx_matlab(query_point_id, :);
scatter(C(neighbors_m_ids, 1), C(neighbors_m_ids, 2), 80, 'g', 'x', 'LineWidth', 2);

title(sprintf('Σύγκριση k-NN για k=%d', k));
legend('Όλα τα σημεία', 'Query', 'C (Pthreads) Γείτονες', 'MATLAB Γείτονες');
hold off;

%% Έλεγχος Σφάλματος
diff_dst = norm(dst_c - dst_matlab, 'fro');
fprintf('Συνολικό σφάλμα (Frobenius norm) μεταξύ C και MATLAB: %f\n', diff_dst);
